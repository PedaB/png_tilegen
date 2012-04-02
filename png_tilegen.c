
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <zlib.h>
#include <png.h>

#define TILE_SIZE 256

typedef struct rgb_ {
  unsigned char r;
  unsigned char g;
  unsigned char b;
} rgb_t;

typedef struct img_ {
  int size;
  int tileX, tileY;
  rgb_t *raw_data;
} img_t;

typedef struct png_ {
  FILE *fp;
  png_structp png_ptr;
  png_infop info_ptr;
} png_t;

static int isPowOfTwo(int v) {
  return !(v & (v-1));
}

static int read_int(FILE *fp) {
  int k;
  char ch;

  do {
    while (isspace(ch = getc(fp)));
    if (ch == '#')
      while ((ch = getc(fp)) != '\n');
  } while (!isdigit(ch));

  for (k = (ch - '0'); isdigit(ch = getc(fp)); k = 10 * k + (ch - '0'));

  return k;
}

static int read_ppm(const char *file, img_t *img) {

  FILE *fp;
  char magic[3];
  int w, h, max;

  fp = fopen(file, "rb");
  if (!fp) return 2;

  fread(magic, 2, 1, fp);

  if (strncmp(magic, "P6", 2) != 0) return 3;

  w = read_int(fp);
  h = read_int(fp);
  max = read_int(fp);

  if (max > 255) return 6;
  if ((w != h) || !isPowOfTwo(w)) return 7;

  img->size = w;
  img->raw_data = malloc(w*w*sizeof(rgb_t));

  fread(img->raw_data, sizeof(rgb_t), w*w, fp);

  return 0;
}

static int read_png(const char *file, img_t *img) {

  FILE *fp;
  png_structp png_ptr;
  png_infop info_ptr;
  int i;

  fp = fopen(file, "rb");
  if (!fp) return 2;

  unsigned char sig[8];
  i = fread(sig, 1, 8, fp);
  if (!i || !png_check_sig(sig, 8)) return 3;

  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr) return 4;
    
  info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) return 5;

  // use longjmp for error handling..
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
    return 6;
  }

  png_init_io(png_ptr, fp);
  png_set_sig_bytes(png_ptr, 8); // tell libpng we already read 8 bytes!

  // third argument: transformation parameters...
  png_read_png(png_ptr, info_ptr, 0, NULL);

  png_uint_32 width = png_get_image_width(png_ptr, info_ptr);
  png_uint_32 height = png_get_image_height(png_ptr, info_ptr);
  if ((width != height) || !isPowOfTwo(width)) return 7;
  
  img->size = width;
  
  //png_uint_32 bitdepth   = png_get_bit_depth(png_ptr, info_ptr);
  png_uint_32 channels   = png_get_channels(png_ptr, info_ptr);
  if (channels != 3) return 8;

  png_uint_32 color_type = png_get_color_type(png_ptr, info_ptr);
  if (color_type != 2) return 9;

  /* DEBUG
  png_uint_32 row_bytes  = png_get_rowbytes(png_ptr, info_ptr);
  printf("depth = %d, channels = %d, color_type = %d, rowbytes = %d\n", bitdepth, channels, color_type, row_bytes);
  */

  png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);

  // allocate data needed to store the image data
  img->raw_data = malloc(width*width*sizeof(rgb_t));

  for (i = 0; i < width; i++) {
    memcpy(&img->raw_data[i*width], row_pointers[i], width*sizeof(rgb_t));
  }
  
  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
  fclose(fp);
  
  return 0;
}

static int open_png_writer(const char *file, png_t *png) {
  
  png->fp = fopen(file, "wb");
  if (!png->fp) return 2;
  
  png->png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png->png_ptr) return 3;
  
  png->info_ptr = png_create_info_struct(png->png_ptr);
  if (!png->info_ptr) return 4;
  
  if (setjmp(png_jmpbuf(png->png_ptr))) {
    png_destroy_write_struct(&png->png_ptr, &png->info_ptr);
    fclose(png->fp);
    return 5;
  }
  
  png_init_io(png->png_ptr, png->fp);
  
  //png_set_compression_level(png->png_ptr, Z_BEST_COMPRESSION);
  //png_set_compression_level(png->png_ptr, Z_BEST_SPEED);

  png_set_IHDR(png->png_ptr, png->info_ptr,
               TILE_SIZE, TILE_SIZE,
               8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, 
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  
  png_write_info(png->png_ptr, png->info_ptr);
  
  return 0;
}

static void close_png_writer(png_t *png) {

  png_write_end(png->png_ptr, png->info_ptr);
  png_destroy_write_struct(&png->png_ptr, &png->info_ptr);
  
  fclose(png->fp);
}

static void crop_image(img_t *img, int zoom, const char *out_dir) {
  
  int ret, i, u, v;
  int tileX, tileY;
  char filename[1024];
  int nr = img->size / TILE_SIZE;
  png_t writer[nr];
  png_bytep row_ptr;

  tileX = img->tileX * nr;

  // create the needed output directory if not present    
  snprintf(filename, 1024, "%s/%d/", out_dir, zoom);
  mkdir(filename, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  for (u = 0; u < nr; u++) {
    snprintf(filename, 1024, "%s/%d/%d/", out_dir, zoom, tileX+u);
    mkdir(filename, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  }

  for (i = 0; i < nr; i++) {
    tileY = img->tileY * nr + i;
    
    // setup writers
    for (u = 0; u < nr; u++) {
      snprintf(filename, 1024, "%s/%d/%d/%d.png", out_dir, zoom, tileX+u, tileY);
      ret = open_png_writer(filename, &writer[u]);
      if (ret) {
        printf("Could not open file \"%s\" for writing! %d\n", filename, ret);
        exit(ret);
      }
    }
    
    for (v = 0; v < TILE_SIZE; v++) {
      row_ptr = (png_bytep) &img->raw_data[(i*TILE_SIZE+v)*img->size];
      for (u = 0; u < nr; u++) {
        png_write_row(writer[u].png_ptr, row_ptr);
        row_ptr += sizeof(rgb_t) * TILE_SIZE;
      }
    }
    
    // close writers
    for (u = 0; u < nr; u++)
      close_png_writer(&writer[u]);
  }
}

static void scale_image(img_t *img) {

  int u, v;
  int size = img->size;
  int new_size = size/2;

  /* 
   * This code code be nicely optimized with sse.
   * However, according to callgrind this function
   * has <<1% cpu time
   */
 
  for (v = 0; v < new_size; v++) {
    for (u = 0; u < new_size; u++) {
        
       rgb_t v0 = img->raw_data[2*u+2*v*size];
       rgb_t v1 = img->raw_data[2*u+1+2*v*size];
       rgb_t v2 = img->raw_data[2*u+(2*v+1)*size];
       rgb_t v3 = img->raw_data[2*u+1+(2*v+1)*size];
       
       int r = ((int) v0.r) + v1.r + v2.r + v3.r;
       int g = ((int) v0.g) + v1.g + v2.g + v3.g;
       int b = ((int) v0.b) + v1.b + v2.b + v3.b;
       
       v0.r = r/4;
       v0.g = g/4;
       v0.b = b/4;
       
       img->raw_data[u+v*new_size] = v0;
    }
  }
  
  img->size /= 2;
}

void main(int argc, char **argv) {
  
  FILE *fp;
  int ret, zoom, tmp;
  img_t img;
  
  if (argc < 6) {
    printf("Usage:\ntilegen <input.png> <output_dir> <tileX> <tileY> <max_zoom> [cardinal_direction N|E|S|W]\n");
    exit(1);
  }
  
  printf("Read file \"%s\"\n", argv[1]);
  if (strstr(argv[1], ".png"))
    ret = read_png(argv[1], &img);
  else if (strstr(argv[1], ".ppm"))
    ret = read_ppm(argv[1], &img);
  else {
    printf("Unknown format!\n");
    exit(2);
  }

  if (ret) {
    printf("Could not read png input file! %d\n", ret);
    exit(ret);
  }

  img.tileX = atoi(argv[3]);
  img.tileY = atoi(argv[4]);
  zoom = atoi(argv[5]);

  if (argc > 6) {
    switch(argv[6][0]) {
    case 'n':
    case 'N':
      // keep defaults
      break;
    case 'e':
    case 'E':
      tmp = img.tileX;
      img.tileX = img.tileY;
      img.tileY = (2<<12) - 1 - tmp;
      break;
    case 's':
    case 'S':
      img.tileX = (2<<12) - 1 - img.tileX;
      img.tileY = (2<<12) - 1 - img.tileY;
      break;
    case 'w':
    case 'W':
      tmp = img.tileX;
      img.tileX = (2<<12) - 1 - img.tileY;
      img.tileY = tmp;
      break;
    default:
      fprintf(stderr, "Unknown cardinal direction \"%s\". Default to north\n", argv[6]);
    }
  }
  
  while (1) {
  
    printf("Processing zoom %d\n", zoom);
    
    // crop image at given zoom level..
    crop_image(&img, zoom, argv[2]);

    // if we had only one tile left we're finished
    if (img.size == TILE_SIZE)
      break;

    scale_image(&img);
    zoom--;
  }
}

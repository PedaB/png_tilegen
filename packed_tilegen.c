
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "common.h"

#define MAP_SIZE 1024+256+64+16+4

int offsets[] = {0, 1024, 1024+256, 1024+256+64, 1024+256+64+16};

// stored offsets for the single png images
static int offset_map[MAP_SIZE]; // == 1364


static int open_png_writer(png_t *png, int sizeY) {
  
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
               TILE_SIZE, sizeY,
               8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, 
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  
  png_write_info(png->png_ptr, png->info_ptr);
  
  return 0;
}


static void close_png_writer(png_t *png) {

  png_write_end(png->png_ptr, png->info_ptr);
  png_destroy_write_struct(&png->png_ptr, &png->info_ptr);
}


static FILE *open_packed_file(const char *output_dir, int tileX, int tileY) {

  char filename[1024];

  // create the needed output directory if not present    
  snprintf(filename, 1024, "%s/packed/", output_dir);
  mkdir(filename, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

  snprintf(filename, 1024, "%s/packed/%d/", output_dir, tileX);
  mkdir(filename, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

  snprintf(filename, 1024, "%s/packed/%d/%d.pack", output_dir, tileX, tileY);
  FILE *pf = fopen(filename, "wb");
  fseek(pf, 1364*4, SEEK_CUR);

  return pf;
}


static void close_packed_file(FILE *file) {
  fseek(file, 0, SEEK_SET);
  fwrite(offset_map, 4, MAP_SIZE, file);
  fclose(file);
}


static void crop_image(img_t *img, int base_offset, FILE *out) {

  int i, u, v;
  int ret, idx;
  int nr = img->sizeX / TILE_SIZE;
  png_bytep png_row_ptr;
  png_t png;
  png.fp = out;

  for (v = 0; v < nr; v++) {
    for (u = 0; u < nr; u++) {
      
      // update offset_map for zoom levels > 13
      if (base_offset < 5) {
	idx = u + v*nr;
	offset_map[idx + base_offset] = (int) ftell(out);
      }

      ret = open_png_writer(&png, TILE_SIZE/(img->sizeX/img->sizeY));

      for (i = 0; i < img->sizeY/nr; i++) {
	png_row_ptr = (png_bytep) &img->raw_data[u+v*(img->sizeY/nr)*img->sizeX+i*img->sizeX];
	png_write_row(png.png_ptr, png_row_ptr);
      }
      close_png_writer(&png);
    }
  }
}

static void save_image(img_t *img, char *output_dir) {
  
  char filename[1024];

  // create the needed output directory if not present    
  snprintf(filename, 1024, "%s/13/", output_dir);
  mkdir(filename, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

  snprintf(filename, 1024, "%s/13/%d/", output_dir, img->tileX);
  mkdir(filename, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

  snprintf(filename, 1024, "%s/13/%d/%d.png", output_dir, img->tileX, img->tileY);
  FILE *file = fopen(filename, "wb");

  crop_image(img, 6, file);

  fclose(file);
}


int main(int argc, char **argv) {

  int ret, tmp;
  img_t img;
  char *output_dir;
  FILE *out_file;

  if (argc < 5) {
    printf("Usage:\npacked_tilegen <input> <output_dir> <tileX> <tileY> [cardinal direction N|E|S|W\n");
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

  output_dir = argv[2];
  
  if (ret) {
    printf("Could not read png input file! %d\n", ret);
    exit(ret);
  }

  img.tileX = atoi(argv[3]);
  img.tileY = atoi(argv[4]);

  if (argc > 5) {
    switch(argv[5][0]) {
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
      fprintf(stderr, "Unknown cardinal direction \"%s\". Default to north\n", argv[5]);
    }
  }

  // create pack file for writing
  out_file = open_packed_file(output_dir, img.tileX, img.tileY);
  tmp = 0;

  while (1) {
    
    // crop image at given zoom level..
    crop_image(&img, tmp++, out_file);

    scale_image(&img);

    if (img.sizeX == TILE_SIZE) {
      save_image(&img, output_dir);
      break;
    }
  }

  close_packed_file(out_file);

  return 0;
}

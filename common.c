
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <png.h>

#include "common.h"

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

int read_ppm(const char *file, img_t *img) {

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
  if (!isPowOfTwo(h) || !isPowOfTwo(w)) return 7;

  img->sizeX = w;
  img->sizeY = h;
  img->raw_data = malloc(w*h*sizeof(rgb_t));

  // TODO: improve and use mmap
  fread(img->raw_data, sizeof(rgb_t), w*h, fp);

  return 0;
}

int read_png(const char *file, img_t *img) {

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
  if (!isPowOfTwo(height) || !isPowOfTwo(width)) return 7;
  
  img->sizeX = width;
  img->sizeY = height;
  
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
  img->raw_data = malloc(height*width*sizeof(rgb_t));

  for (i = 0; i < height; i++) {
    memcpy(&img->raw_data[i*width], row_pointers[i], width*sizeof(rgb_t));
  }
  
  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
  fclose(fp);
  
  return 0;
}

void scale_image(img_t *img) {

  int u, v;
  const int sizeX = img->sizeX;
  const int sizeY = img->sizeY;

  /* 
   * This code code be nicely optimized with sse.
   * However, according to callgrind this function
   * has <<1% cpu time
   */
 
  for (v = 0; v < sizeY/2; v++) {
    for (u = 0; u < sizeX/2; u++) {
        
       rgb_t v0 = img->raw_data[2*u+2*v*sizeX];
       rgb_t v1 = img->raw_data[2*u+1+2*v*sizeX];
       rgb_t v2 = img->raw_data[2*u+(2*v+1)*sizeX];
       rgb_t v3 = img->raw_data[2*u+1+(2*v+1)*sizeX];
       
       int r = ((int) v0.r) + v1.r + v2.r + v3.r;
       int g = ((int) v0.g) + v1.g + v2.g + v3.g;
       int b = ((int) v0.b) + v1.b + v2.b + v3.b;
       
       v0.r = r/4;
       v0.g = g/4;
       v0.b = b/4;
       
       img->raw_data[u+v*sizeX/2] = v0;
    }
  }
  
  img->sizeX /= 2;
  img->sizeY /= 2;
}



#define TILE_SIZE 256

#define QUALITY 90

#define PNG 0
#define JPG 1

typedef struct rgb_ {
  unsigned char r;
  unsigned char g;
  unsigned char b;
} rgb_t;

typedef struct img_ {
  int sizeX, sizeY;
  int tileX, tileY;
  rgb_t *raw_data;
} img_t;


int read_ppm(const char *file, img_t *img);
int read_png(const char *file, img_t *img);

void scale_image(img_t *img);


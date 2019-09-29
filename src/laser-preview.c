/* Uses the simplified API, thus requires libpng 1.6 or above */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>


/* describes an image with upgradable dimensions, possibly supporting negative
 * coordinates.
 */
struct img {
	int x0, x1; // x0 <= x1
	int y0, y1; // y0 <= y1
	float *area;
};


/* display the message and exit with the code */
__attribute__((noreturn)) void die(int code, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	exit(code);
}

/* write the buffer as a <width>x<height> grayscale image into file <file>,
 * or to stdout if <file> is NULL. The image will go from top to bottom to
 * accommodate from GCODE's image directions, but this can be changed by
 * setting the row_stride argument to 1 instead of -1. Returns non-zero on
 * success, otherwise zero.
 */
int write_gs_file(const char *file, int width, int height, const uint8_t *buffer)
{
	const int row_stride = -1; // bottom to top
	png_image image;
	int ret;

	memset(&image, 0, sizeof(image));
	image.version = PNG_IMAGE_VERSION;
	image.width   = width;
	image.height  = height;
	image.format  = PNG_FORMAT_GRAY;

	if (file)
		ret = png_image_write_to_file(&image, file, 0, buffer, row_stride * width, NULL);
	else
		ret = png_image_write_to_stdio(&image, stdout, 0, buffer, row_stride * width, NULL);
	return ret;
}


/* crops a grayscale image in buffer <buffer>, expected to be <w>x<h> large, to
 * keep only (<x0>,<y0)-(<x1>,<y1>), all included. First columns and rows are
 * numbered zero. Returns the output size in bytes on success, 0 on failure.
 * x0, x1, y0, y1 must be within the original buffer dimensions, with x0<=x1,
 * y0<=y1.
 */
int crop_gs_image(uint8_t *buffer, int w, int h, int x0, int y0, int x1, int y1)
{
	int row_pre, row_post;
	uint8_t *src, *dst;

	const int row_stride = -1; // bottom to top
	png_image image;
	int x, y, ret;

	if (w <= 0 || x0 < 0 || x1 < 0 || x0 >= w || x1 >= w || x0 > x1)
		return 0;

	if (h <= 0 || y0 < 0 || y1 < 0 || y0 >= h || y1 >= h || y0 > y1)
		return 0;

	row_pre = x0;
	row_post = h - 1 - x1;

	src = dst = buffer;
	src += y0 * w;
	for (y = y0; y <= y1; y++) {
		src += row_pre;
		for (x = x0; x <= x1; x++)
			*dst++ = *src++;
		src += row_post;
	}

	return dst - buffer;
}

/* Extend img to cover (nx0,ny0)-(nx1,ny1) instead of img->(x0,y0)-(x1,y1).
 * Shrinking is not supported and will be ignored. Returns non-zero on success,
 * 0 on error (typically due to memory allocation). If the original buffer's
 * area is not allocated, the image is considered not initialized and it will
 * be initialized and allocated from the arguments.
 */
int extend_img(struct img *img, int nx0, int ny0, int nx1, int ny1)
{
	float *new_area;
	int nw, nh;
	int ow, oh;
	int x, y;

	if (img->area) {
		if (nx0 > img->x0)
			nx0 = img->x0;

		if (ny0 > img->y0)
			ny0 = img->y0;

		if (nx1 < img->x1)
			nx1 = img->x1;

		if (ny1 < img->y1)
			ny1 = img->y1;

		if (nx0 == img->x0 && ny0 == img->y0 && nx1 == img->x1 && ny1 == img->y1)
			return 1;

		ow = img->x1 + 1 - img->x0;
		oh = img->y1 + 1 - img->y0;
	}

	nw = nx1 + 1 - nx0;
	nh = ny1 + 1 - ny0;

	new_area = calloc(nw * nh, sizeof(*img->area));
	if (!new_area)
		return 0;

	if (img->area) {
		for (y = img->y0; y <= img->y1; y++) {
			for (x = img->x0; x <= img->x1; x++) {
				new_area[(y - ny0) * nw + (x - nx0)] =
					img->area[(y - img->y0) * ow + (x - img->x0)];
			}
		}
		free(img->area);
	}

	img->x0 = nx0;
	img->y0 = ny0;
	img->x1 = nx1;
	img->y1 = ny1;
	img->area = new_area;
	return 1;
}

int main(int argc, char **argv)
{
	uint8_t *buffer;
	const char *file;
	struct img img;
	int w, h;
	int x, y;
	int ret;

	file = NULL;
	w = 1000;
	h = 1000;

	if (argc > 1 && *argv[1])
		file = argv[1];

	if (argc > 2)
		w = atoi(argv[2]);

	if (argc > 3)
		h = atoi(argv[3]);

	memset(&img, 0, sizeof(img));
	if (!extend_img(&img, 0, 0, w-1, h-1))
		die(1, "out of memory\n");

	buffer = calloc(w * h, 1);
	if (!buffer)
		die(1, "out of memory\n");

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			img.area[y * w + x] = 0.5 * y / h + 0.5 * x / w;
		}
	}

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			buffer[y * w + x] = img.area[y * w + x] * 255.0;
		}
	}

	crop_gs_image(buffer, w, h, 100, 100, w - 1 - 100, h - 1 - 100);

	ret = write_gs_file(file, w-200, h-200, buffer);
	if (!ret)
		die(1, "failed to write file\n");
	return 0;
}

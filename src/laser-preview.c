/* Uses the simplified API, thus requires libpng 1.6 or above */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>


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
 * keep only (<x1>,<y1)-(<x2>,<y2>), all included. First columns and rows are
 * numbered zero. Returns the output size in bytes on success, 0 on failure.
 * x1, x2, y1, y2 must be within the original buffer dimensions, with x1<=x2,
 * y1<=y2.
 */
int crop_gs_image(uint8_t *buffer, int w, int h, int x1, int y1, int x2, int y2)
{
	int row_pre, row_post;
	uint8_t *src, *dst;

	const int row_stride = -1; // bottom to top
	png_image image;
	int x, y, ret;

	if (w <= 0 || x1 < 0 || x2 < 0 || x1 >= w || x2 >= w || x1 > x2)
		return 0;

	if (h <= 0 || y1 < 0 || y2 < 0 || y1 >= h || y2 >= h || y1 > y2)
		return 0;

	row_pre = x1;
	row_post = h - 1 - x2;

	src = dst = buffer;
	src += y1 * w;
	for (y = y1; y <= y2; y++) {
		src += row_pre;
		for (x = x1; x <= x2; x++)
			*dst++ = *src++;
		src += row_post;
	}

	return dst - buffer;
}

int main(int argc, char **argv)
{
	uint8_t *buffer;
	const char *file;
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

	buffer = calloc(w * h, 1);
	if (!buffer)
		die(1, "out of memory\n");

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			buffer[y * w + x] = 127.5 * y / h + 127.5 * x / w;
		}
	}

	crop_gs_image(buffer, w, h, 100, 100, w - 1 - 100, h - 1 - 100);

	ret = write_gs_file(file, w-200, h-200, buffer);
	if (!ret)
		die(1, "failed to write file\n");
	return 0;
}

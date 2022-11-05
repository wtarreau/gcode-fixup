/* Uses the simplified API, thus requires libpng 1.6 or above */
#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

/* default settings */
#define DEFAULT_WIDTH            0
#define DEFAULT_HEIGHT           0
#define DEFAULT_LIN_DIFF         0.25
#define DEFAULT_PIX_SIZE         0.1
#define DEFAULT_BEAM_POWER       10.0    // Watts
#define DEFAULT_ENERGY_DENSITY   0.5     // J/mm^2

/* clear wood: little absorption first, then takes way more once
 * already burnt.
 */
#define DEFAULT_ABSORPTION        0.75
#define DEFAULT_ABSORPTION_FACTOR 2.0

const struct option long_options[] = {
	{"help",        no_argument,       0, 'h'              },
	{"diffusion",   required_argument, 0, 'd'              },
	{"width",       required_argument, 0, 'W'              },
	{"height",      required_argument, 0, 'H'              },
	{"multiply",    required_argument, 0, 'm'              },
	{"output",      required_argument, 0, 'o'              },
	{"pixel-size",  required_argument, 0, 'p'              },
	{"beam-power",  required_argument, 0, 'P'              },
	{"energy-density", required_argument, 0, 'e'           },
	{0,             0,                 0, 0                }
};

/* describes an image with upgradable dimensions, possibly supporting negative
 * coordinates.
 */
struct img {
	int x0, x1; // x0 <= x1
	int y0, y1; // y0 <= y1
	float *area;
	float absorption; // 0..1, depends on the material
	float absorption_factor; //-x..+x, depends on the material
	float diffusion_lin;     // linear diffusion (ratio of power sent over 1px dist)
	float diffusion_dia;     // diagonal diffusion (lin^sqrt(2)).
	float diffusion;         // diffusion factor so that 4lin+4dia+diff == 1.0
	double pixel_size;       // pixel size in mm
	float pixel_energy;      // energy per pixel in Joule
	float beam_power;        // beam power in watts
	float energy_density;    // minimum marking energy in J/px^2
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

	//printf("x0=%d->%d x1=%d->%d y0=%d->%d y1=%d->%d\n",
	//       img->x0, nx0, img->x1, nx1, img->y0, ny0, img->y1, ny1);

	/* make sure coordinates are not swapped */
	if (nx0 > nx1) {
		x = nx0;
		nx0 = nx1;
		nx1 = x;
	}

	if (ny0 > ny1) {
		y = ny0;
		ny0 = ny1;
		ny1 = y;
	}

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

	nw = nx1 + 1 - nx0;
	nh = ny1 + 1 - ny0;

	if (ow == nw && ny0 == img->y0) {
		new_area = realloc(img->area, nw * nh * sizeof(*img->area));
		if (new_area) {
			memset(&new_area[oh * nw], 0, (nh - oh) * nw * sizeof(*img->area));
			img->area = NULL;
		}
	}
	else
		new_area = calloc(nw * nh, sizeof(*img->area));

	if (!new_area)
		return 0;

	if (img->area) {
		for (y = img->y0; y <= img->y1; y++) {
			memcpy(&new_area[(y - ny0) * nw + (img->x0 - nx0)],
			       &img->area[(y - img->y0) * ow],
			       ow * sizeof(*new_area));
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

/* add energy <value> to pixel at <x,y> */
static inline void add_to_pixel(struct img *img, int x0, int y0, float value)
{
	int nx0, ny0, nx1, ny1;

	nx0 = (x0 < img->x0) ? x0 : img->x0;
	nx1 = (x0 > img->x1) ? x0 : img->x1;
	ny0 = (y0 < img->y0) ? y0 : img->y0;
	ny1 = (y0 > img->y1) ? y0 : img->y1;

	if (nx0 != img->x0 || nx1 != img->x1 || ny0 != img->y0 || ny1 != img->y1) {
		if (!extend_img(img, nx0, ny0, nx1, ny1))
			return;
	}

	img->area[(y0 - img->y0) * (img->x1 - img->x0 + 1) + (x0 - img->x0)] += value * img->diffusion;

	if (value < 0.05)
		return;

	add_to_pixel(img, x0 - 1, y0 - 1, value * img->diffusion_dia * img->diffusion);
	add_to_pixel(img, x0 + 0, y0 - 1, value * img->diffusion_lin * img->diffusion);
	add_to_pixel(img, x0 + 1, y0 - 1, value * img->diffusion_dia * img->diffusion);

	add_to_pixel(img, x0 - 1, y0 + 0, value * img->diffusion_lin * img->diffusion);
	add_to_pixel(img, x0 + 1, y0 + 0, value * img->diffusion_lin * img->diffusion);

	add_to_pixel(img, x0 - 1, y0 + 1, value * img->diffusion_dia * img->diffusion);
	add_to_pixel(img, x0 + 0, y0 + 1, value * img->diffusion_lin * img->diffusion);
	add_to_pixel(img, x0 + 1, y0 + 1, value * img->diffusion_dia * img->diffusion);
}

/* mark the 1x1 area around (x,y) as burnt, taking the intensity and overlap
 * into account. There can be up to 4 pixels affected.
 */
static inline int burn(struct img *img, double x, double y, float intensity)
{
	int x0, y0, x1, y1, w;
	float s00, s01, s10, s11; // fraction of overlapping surface
	float t00, t01, t10, t11; // energy thresholds to mark the pixel
	float pix_energy;         // pixel energy in J
	double dx, dy;

	/* depending on the rounding resulting from non-integer pixel sizes, we
	 * can have some rounding issues below due to tiny fractional parts
	 * causing some pixels to happen at the wrong place (often a line).
	 * We don't need too much sub-pixel precision, and rounding to 1/16
	 * of a pixel seems to solve all problems even with pixels of 7/80mm.
	 */
	x = round(x * 16.0) / 16.0;
	y = round(y * 16.0) / 16.0;

	x0 = (int)floor(x);
	x1 = x0 + 1;

	y0 = (int)floor(y);
	y1 = y0 + 1;

	if (x0 < img->x0 || x1 > img->x1 || y0 < img->y0 || y1 > img->y1) {
		if (!extend_img(img, x0, y0, x1, y1))
			return 0;
	}

	w  = img->x1 - img->x0 + 1;

	/* We consider that pixels are centered like this:
	 * x=0 y=0 : covers area [0,0]->]1,1[ centered on (0.5, 0.5)
	 * x=1 y=0 : covers area [0,1]->]2,1[ centered on (1.5, 0.5)
	 * x=0 y=1 : covers area [0,1]->]1,2[ centered on (0.5, 1.5)
	 * x=1 y=1 : covers area [1,1]->]2,2[ centered on (1.5, 1.5)
	 *
	 * The distance between the point and the center of the pixel is
	 * sqrt((px-x)^2 + (py-y)^2) where (x,y) are expected to be shifted
	 * by 0.5 up so that (x=0, y=0) exactly matches pixel [0,0].
	 * The distance cannot exceed sqrt(2), thus we normalize it so that
	 * (1-distance) gives the intensity for each pixel.
	 */
	dx = x - (x0 + 0.5); // [0..1]
	dy = y - (y0 + 0.5); // [0..1]

	s00 =       (dx) * (1.0 - dy);
	s01 = (1.0 - dx) * (1.0 - dy);
	s10 =       (dx) *       (dy);
	s11 = (1.0 - dx) *       (dy);

	//printf("x=%1.2f x0=%d x1=%d y=%1.2f y0=%d y1=%d s00=%1.1f s01=%1.1f s10=%1.1f s11=%1.1f dx=%1.1f dy=%1.1f\n",
	//       x, x0, x1, y, y0, y1, s00, s01, s10, s11, dx, dy);

	/* next steps: count energy delivered by the beam as intensity * time * ratio * absorption.
	 * For now, time has to be passed as part of the intensity by the caller. The absorption
	 * depends on the material and the previous intensity applied to an absorption factor.
	 * Typically painted aluminum will have a 1.0 absorption and a -1.0 factor indicating it
	 * doesn't absorb anymore once fully engraved, while cleaer wood will have 0.25 and a 2.0
	 * factor indicating it becomes much more sensitive once already engraved.
	 */
	s00 *= img->absorption + img->absorption_factor * img->area[(y0 - img->y0) * w + (x0 - img->x0)];
	s01 *= img->absorption + img->absorption_factor * img->area[(y0 - img->y0) * w + (x1 - img->x0)];
	s10 *= img->absorption + img->absorption_factor * img->area[(y1 - img->y0) * w + (x0 - img->x0)];
	s11 *= img->absorption + img->absorption_factor * img->area[(y1 - img->y0) * w + (x1 - img->x0)];

	t00 = img->energy_density * (1.0 - sqrt(img->area[(y0 - img->y0) * w + (x0 - img->x0)]));
	t01 = img->energy_density * (1.0 - sqrt(img->area[(y0 - img->y0) * w + (x1 - img->x0)]));
	t10 = img->energy_density * (1.0 - sqrt(img->area[(y1 - img->y0) * w + (x0 - img->x0)]));
	t11 = img->energy_density * (1.0 - sqrt(img->area[(y1 - img->y0) * w + (x1 - img->x0)]));

	if (img->absorption_factor < 0.0) {
		if (s00 < 0.0) s00 = 0.0;
		if (s01 < 0.0) s01 = 0.0;
		if (s10 < 0.0) s10 = 0.0;
		if (s11 < 0.0) s11 = 0.0;
	}

	s00 *= intensity;
	s01 *= intensity;
	s10 *= intensity;
	s11 *= intensity;

	if (s00 > 1.0) s00 = 1.0;
	if (s01 > 1.0) s01 = 1.0;
	if (s10 > 1.0) s10 = 1.0;
	if (s11 > 1.0) s11 = 1.0;

	/* let's calculate this pixel's energy and the marking threshold */
	pix_energy = intensity * img->pixel_energy;

	//printf("pix_energy=%1.6f t01=%1.6f s01=%1.6f\n", pix_energy, t01, s01);
	/* now sXX contains the amount of energy delivered over pixel XX. For
	 * now we don't really care if areas are overburnt, better properly
	 * count the delivered energy.
	 */
	if (pix_energy >= t00)
		add_to_pixel(img, x0, y0, s00);
	if (pix_energy >= t01)
		add_to_pixel(img, x1, y0, s01);
	if (pix_energy >= t10)
		add_to_pixel(img, x0, y1, s10);
	if (pix_energy >= t11)
		add_to_pixel(img, x1, y1, s11);

	/* Then we have diffusion to surrounding pixels, which is a function of their distance
	 * and depends on the material. Long dispersion means the energy is exchanged to other
	 * places thus there is less locally and more remotely. Short dispersion means we only
	 * burn under the beam. Ideally the dispersion should take the time into account so that
	 * we can apply it with the feed speed, allowing the local spot to cool down and not
	 * reach a burning temperature.
	 */
	return 1;
}

/* Draw a vector in <img> from (x0,y0) to (x1,y1) included at intensity
 * <intensity>. The principle consists in cutting the vector into 1-px large
 * steps (vert or horiz) and assigning the beam energy in the middle of each
 * segment, with the principle that the beam is 1x1 px centered in the middle
 * of a pixel around (0.5, 0.5), where all the energy is sent. The area under
 * the beam will get its share of the energy depending on how it overlaps.
 *
 * Thus when asking to draw a segment between pixels (0,0) and (2,1), a 2-pixel
 * segment will be drawn between locations (0.5,0.5) and (2.5,1.5), split like
 * this:
 *   1st segment: (0.5,0.5)-(1.5,1.0)
 *   2nd segment: (1.5,1.0)-(2.5,1.5)
 * resulting in the following locations to be burnt:
 *   - 1x1 around (1.0,0.75) = (0.5,0.25)-(1.5,1.25)
 *   - 1x1 around (2.0,1.25) = (1.5,0.75)-(2.5,1.75)
 *
 * Returns non-zero if OK, 0 on error.
 *
 * Example, with movement from location F (0,0) to T (4,2):
 *
 *     0   1   2   3   4      1st step: move from (0,0) to (1,0.5)
 *   +---+---+---+---+---+     = beam center from (0.5,0.5) to (1.5,1.0)
 * 2 |   |   |   |   | T |    -> beam spot (1.0, 0.75) -> (0.5,0.25)-(1.5,1.25)
 *   +---+---+---+-/-+---+    +---+---+
 * 1 |   |   | / |   |   |    | .---. | < 1.75
 *   +---+-/-+---+---+---+    +-|-+-|-+
 * 0 | F |   |   |   |   |    | '---' | < 0.75
 *   +---+---+---+---+---+    +---+---+
 *
 */
int draw_vector(struct img *img, double x0, double y0, double x1, double y1, double intensity)
{
	double dx = x1 - x0;
	double dy = y1 - y0;

	if (!dx && !dy)
		return 1;

	if (fabs(dx) >= fabs(dy)) {
		/* must visit all X places */
		double x, y;

		if (dx < 0) {
			dx = -dx;
			x0 = x1;
			x1 = x0 + dx;
		}

		for (x = x0 + 0.5; x < x1 + 0.5; x += 1.0) {
			/* aim the beam at (x,y) */
			y = y0 + 0.5 + (x - x0 + 0.5 /* for mid-trip */) * dy / dx;
			/* So beam overlaps with (x-0.5,y-0.5,x+0.5,y+0.5) */
			if (!burn(img, x, y, intensity))
				return 0;
		}
	} else {
		/* must visit all Y places */
		double x, y;

		if (dy < 0) {
			dy = -dy;
			y0 = y1;
			y1 = y0 + dy;
		}

		for (y = y0 + 0.5; y < y1 + 0.5; y += 1.0) {
			/* aim the beam at (x, y+0.5) */
			x = x0 + 0.5 + (y - y0 + 0.5 /* for mid-trip */) * dx / dy;
			/* So beam overlaps with (x-0.5,y-0.5,x+0.5,y+0.5) */
			if (!burn(img, x, y, intensity))
				return 0;
		}
	}
}

/* minimalistic parsing of a gcode file, applying <power> as a power ratio, and
 * zoom to x & y coordinates.
 * The feed time is not taken into account, only the spindle speed. Returns 0
 * on error otherwise the number of lines read.
 */
int parse_gcode(struct img *img, FILE *file, double zoom, float power)
{
	char line[1024];
	char *p, *e;
	double val;
	int drawing = 0;
	double spindle;
	double new_x = 0, new_y = 0;
	double cur_x = 0, cur_y = 0;
	int cur_s = 0;

	while (fgets(line, sizeof(line), file) != NULL) {
		for (p = line; *p; p = e) {
			while (*p == ' ')
				p++;

			for (e = p; *e; e++) {
				if (*e == '\n' || *e == ';') {
					*e = 0;
					break;
				}
				if (*e == ' ') {
					*e++ = 0;
					break;
				}
			}
			/* we have a word at <p> and <e> points to the next one */
			*p = toupper(*p);
			val = atof(p + 1);
			if (*p == 'G') {
				if (val == 0)
					drawing = 0;
				else if (val >= 1 && val <=3)
					drawing = 1;
			}
			else if (*p == 'M') {
				if (val == 3 || val == 4) {
					drawing = 1;
					cur_s = 255;
				}
				else if (val == 5)
					drawing = 0;
			}
			else if (*p == 'X') {
				new_x = floor(val * zoom + zoom / 16);
			}
			else if (*p == 'Y') {
				new_y = floor(val * zoom + zoom / 16);
			}
			else if (*p == 'S') {
				cur_s = val;
			}
			else if (*p == 'F' && val > 0.0) {
				// speed in mm/mn. Div 60 for mm/s. Power in Watts = J/s.
				// pxsz in mm/px, thus P/(F/60) = J/mm. P*pxsz*60/F = J/px.
				img->pixel_energy = img->beam_power * img->pixel_size * 60.0 / val;
			}
		}

		if (drawing && (new_x != cur_x || new_y != cur_y)) {
			draw_vector(img, cur_x, cur_y, new_x, new_y, cur_s / 255.0 * power);
		}

		cur_x = new_x;
		cur_y = new_y;
	}
	return 1;
}

void usage(int code, const char *cmd)
{
	die(code,
	    "\n"
	    "Usage: %s [options]*\n"
	    "  -h --help                    show this help\n"
	    "  -H --height <size>           output image minimum height in pixels (def: 0)\n"
	    "  -W --width <size>            output image minimum width in pixels (def: 0)\n"
	    "  -a --absorption <value>      absorption (def: 0.75 for clear wood)\n"
	    "  -b --beam-power <value>      beam power in Watts (default: 10)\n"
	    "  -e --energy-density <value>  minimum energy density in J/mm^2 (def: 0.5)\n"
	    "  -A --absorption_mul <value>  absorption factor once marked (def: 2.0 for wood)\n"
	    "  -d --diffusion <value>       linear diffusion ratio (def: 0.25)\n"
	    "  -m --multiply <value>        multiply input value by this (def: 1.0)\n"
	    "  -o --output <file>           output PNG file name (default: none=stdout)\n"
	    "  -p --pixel-size <size>       pixel-size in millimeters (default: 0.1)\n"
	    "\n", cmd);
}

int main(int argc, char **argv)
{
	uint8_t *buffer;
	const char *file;
	struct img img;
	float energy_density = DEFAULT_ENERGY_DENSITY;
	double multiply = 1.0;
	int w, h;
	int x, y;
	int ret;

	memset(&img, 0, sizeof(img));

	file = NULL;
	w = DEFAULT_WIDTH;
	h = DEFAULT_HEIGHT;
	img.pixel_size = DEFAULT_PIX_SIZE;
	img.diffusion_lin = DEFAULT_LIN_DIFF;
	img.absorption = DEFAULT_ABSORPTION;
	img.absorption_factor = DEFAULT_ABSORPTION_FACTOR;
	img.beam_power = DEFAULT_BEAM_POWER;

	while (1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "ha:A:d:e:m:o:p:P:W:H:", long_options, &option_index);
		double arg_f = optarg ? atof(optarg) : 0.0;
		int arg_i   = optarg ? atoi(optarg) : 0;

		if (c == -1)
			break;

		switch (c) {
		case 0: /* long option: long_options[option_index] with arg <optarg> */
			break;

		case 'a':
			img.absorption = arg_f;
			break;

		case 'A':
			img.absorption_factor = arg_f;
			break;

		case 'd':
			img.diffusion_lin = arg_f;
			break;

		case 'e':
			energy_density = arg_f;
			break;

		case 'h':
			usage(0, argv[0]);
			break;

		case 'm':
			multiply = arg_f;
			break;

		case 'o' :
			file = optarg;
			break;

		case 'p':
			if (arg_f > 0.0)
				img.pixel_size = arg_f;
			break;

		case 'P':
			img.beam_power = arg_f;
			break;

		case 'W':
			w = arg_i;
			break;

		case 'H':
			h = arg_i;
			break;
			break;

		case ':': /* missing argument */
		case '?': /* unknown option */
			die(1, "");
		}
	}

	img.energy_density = energy_density * img.pixel_size * img.pixel_size;
	img.diffusion_dia = powf(img.diffusion_lin, sqrt(2));
	img.diffusion = 1.0 / (1.0 + 4.0 * img.diffusion_dia + 4.0 * img.diffusion_lin);
	/* thus we have diff*(1+4*dia+4*lin) = 1 */
	printf("dif=%f lin=%f dia=%f\n", img.diffusion, img.diffusion_lin, img.diffusion_dia);

	if (!extend_img(&img, 0, 0, w-1, h-1))
		die(1, "out of memory\n");

	/* gradient for experimentation */
	//for (y = 0; y < h; y++) {
	//	for (x = 0; x < w; x++) {
	//		img.area[y * w + x] = 0.5 * y / h + 0.5 * x / w;
	//	}
	//}

	//draw_vector(&img, 125, 125, 500, 600, 10.0);
	//draw_vector(&img, 125, 125, 600, 600, 10.0);
	//draw_vector(&img, 125, 125, 600, 500, 10.0);

	if (!parse_gcode(&img, stdin, 1.0 / img.pixel_size, multiply))
		die(1, "failed to process gcode");

	printf("x0=%d y0=%d x1=%d y1=%d\n", img.x0, img.y0, img.x1, img.y1);

	w = img.x1 - img.x0 + 1;
	h = img.y1 - img.y0 + 1;

	/* let's now recompute the new image size and allocate the PNG buffer */
	w = img.x1 - img.x0 + 1;
	h = img.y1 - img.y0 + 1;

	buffer = calloc(w * h, 1);
	if (!buffer)
		die(1, "out of memory\n");

	/* convert the work area to a PNG buffer */
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			float v = img.area[y * w + x];
			if (v < 0.0)
				v = 0.0;
			else if (v > 1.0)
				v = 1.0;
			buffer[y * w + x] = 255 - v * 255.0;
		}
	}

	//crop_gs_image(buffer, w, h, 100, 100, w - 1 - 100, h - 1 - 100);
	//ret = write_gs_file(file, w-200, h-200, buffer);

	ret = write_gs_file(file, w, h, buffer);
	if (!ret)
		die(1, "failed to write file\n");
	return 0;
}

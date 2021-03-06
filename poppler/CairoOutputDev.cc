//========================================================================
//
// CairoOutputDev.cc
//
// Copyright 2003 Glyph & Cog, LLC
// Copyright 2004 Red Hat, Inc
//
//========================================================================

#include <config.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <string.h>
#include <math.h>
#include <assert.h>
#include <cairo.h>

#include "goo/gfile.h"
#include "GlobalParams.h"
#include "Error.h"
#include "Object.h"
#include "GfxState.h"
#include "GfxFont.h"
#include "Link.h"
#include "CharCodeToUnicode.h"
#include "FontEncodingTables.h"
#include <fofi/FoFiTrueType.h>
#include <splash/SplashBitmap.h>
#include "CairoOutputDev.h"
#include "CairoFontEngine.h"
//------------------------------------------------------------------------

// #define LOG_CAIRO

#ifdef LOG_CAIRO
#define LOG(x) (x)
#else
#define LOG(x)
#endif

static inline void printMatrix(cairo_matrix_t *matrix){
	printf("%f %f, %f %f (%f %f)\n", matrix->xx, matrix->yx,
			matrix->xy, matrix->yy,
			matrix->x0, matrix->y0);
}

//------------------------------------------------------------------------
// CairoImage
//------------------------------------------------------------------------

CairoImage::CairoImage (cairo_surface_t *image,
			double x1, double y1, double x2, double y2) {
  this->image = cairo_surface_reference (image);
  this->x1 = x1;
  this->y1 = y1;
  this->x2 = x2;
  this->y2 = y2;
}

CairoImage::~CairoImage () {
  if (image)
    cairo_surface_destroy (image);
}

//------------------------------------------------------------------------
// CairoOutputDev
//------------------------------------------------------------------------

CairoOutputDev::CairoOutputDev() {
  xref = NULL;

  FT_Init_FreeType(&ft_lib);
  fontEngine = NULL;
  glyphs = NULL;
  fill_pattern = NULL;
  stroke_pattern = NULL;
  stroke_opacity = 1.0;
  fill_opacity = 1.0;
  textClipPath = NULL;
  cairo = NULL;
  currentFont = NULL;
  prescaleImages = gTrue;

  groupColorSpaceStack = NULL;
  group = NULL;
  mask = NULL;
}

CairoOutputDev::~CairoOutputDev() {
  if (fontEngine) {
    delete fontEngine;
  }
  FT_Done_FreeType(ft_lib);
  
  if (cairo)
    cairo_destroy (cairo);
  cairo_pattern_destroy (stroke_pattern);
  cairo_pattern_destroy (fill_pattern);
}

void CairoOutputDev::setCairo(cairo_t *cairo)
{
  if (this->cairo != NULL) {
    cairo_status_t status = cairo_status (this->cairo);
    if (status) {
      warning("cairo context error: %s\n", cairo_status_to_string(status));
    }
    cairo_destroy (this->cairo);
  }
  if (cairo != NULL) {
    this->cairo = cairo_reference (cairo);
	/* save the initial matrix so that we can use it for type3 fonts. */
	//XXX: is this sufficient? could we miss changes to the matrix somehow?
	cairo_get_matrix(cairo, &orig_matrix);
  } else {
    this->cairo = NULL;
  }
}

void CairoOutputDev::startDoc(XRef *xrefA) {
  xref = xrefA;
  if (fontEngine) {
    delete fontEngine;
  }
  fontEngine = new CairoFontEngine(ft_lib);
}

void CairoOutputDev::drawLink(Link *link, Catalog *catalog) {
}

void CairoOutputDev::saveState(GfxState *state) {
  LOG(printf ("save\n"));
  cairo_save (cairo);
}

void CairoOutputDev::restoreState(GfxState *state) {
  LOG(printf ("restore\n"));
  cairo_restore (cairo);

  /* These aren't restored by cairo_restore() since we keep them in
   * the output device. */
  updateFillColor(state);
  updateStrokeColor(state);
  updateFillOpacity(state);
  updateStrokeOpacity(state);
}

void CairoOutputDev::updateAll(GfxState *state) {
  updateLineDash(state);
  updateLineJoin(state);
  updateLineCap(state);
  updateLineWidth(state);
  updateFlatness(state);
  updateMiterLimit(state);
  updateFillColor(state);
  updateStrokeColor(state);
  updateFillOpacity(state);
  updateStrokeOpacity(state);
  needFontUpdate = gTrue;
}

void CairoOutputDev::setDefaultCTM(double *ctm) {
  cairo_matrix_t matrix;
  matrix.xx = ctm[0];
  matrix.yx = ctm[1];
  matrix.xy = ctm[2];
  matrix.yy = ctm[3];
  matrix.x0 = ctm[4];
  matrix.y0 = ctm[5];

  cairo_transform (cairo, &matrix);

  OutputDev::setDefaultCTM(ctm);
}

void CairoOutputDev::updateCTM(GfxState *state, double m11, double m12,
				double m21, double m22,
				double m31, double m32) {
  cairo_matrix_t matrix;
  matrix.xx = m11;
  matrix.yx = m12;
  matrix.xy = m21;
  matrix.yy = m22;
  matrix.x0 = m31;
  matrix.y0 = m32;

  cairo_transform (cairo, &matrix);
  updateLineDash(state);
  updateLineJoin(state);
  updateLineCap(state);
  updateLineWidth(state);
}

void CairoOutputDev::updateLineDash(GfxState *state) {
  double *dashPattern;
  int dashLength;
  double dashStart;

  state->getLineDash(&dashPattern, &dashLength, &dashStart);
  cairo_set_dash (cairo, dashPattern, dashLength, dashStart);
}

void CairoOutputDev::updateFlatness(GfxState *state) {
  // cairo_set_tolerance (cairo, state->getFlatness());
}

void CairoOutputDev::updateLineJoin(GfxState *state) {
  switch (state->getLineJoin()) {
  case 0:
    cairo_set_line_join (cairo, CAIRO_LINE_JOIN_MITER);
    break;
  case 1:
    cairo_set_line_join (cairo, CAIRO_LINE_JOIN_ROUND);
    break;
  case 2:
    cairo_set_line_join (cairo, CAIRO_LINE_JOIN_BEVEL);
    break;
  }
}

void CairoOutputDev::updateLineCap(GfxState *state) {
  switch (state->getLineCap()) {
  case 0:
    cairo_set_line_cap (cairo, CAIRO_LINE_CAP_BUTT);
    break;
  case 1:
    cairo_set_line_cap (cairo, CAIRO_LINE_CAP_ROUND);
    break;
  case 2:
    cairo_set_line_cap (cairo, CAIRO_LINE_CAP_SQUARE);
    break;
  }
}

void CairoOutputDev::updateMiterLimit(GfxState *state) {
  cairo_set_miter_limit (cairo, state->getMiterLimit());
}

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

void CairoOutputDev::updateLineWidth(GfxState *state) {
  LOG(printf ("line width: %f\n", state->getLineWidth()));
  if (state->getLineWidth() == 0.0) {
    /* find out how big pixels (device unit) are in the x and y directions
     * choose the smaller of the two as our line width */
    double x = 1.0, y = 1.0;
    cairo_device_to_user_distance(cairo, &x, &y);
    cairo_set_line_width (cairo, MIN(fabs(x),fabs(y)));
  } else {
    cairo_set_line_width (cairo, state->getLineWidth());
  }
}

void CairoOutputDev::updateFillColor(GfxState *state) {
  state->getFillRGB(&fill_color);

  cairo_pattern_destroy(fill_pattern);
  fill_pattern = cairo_pattern_create_rgba(fill_color.r / 65535.0,
					   fill_color.g / 65535.0,
					   fill_color.b / 65535.0,
					   fill_opacity);

  LOG(printf ("fill color: %d %d %d\n",
	      fill_color.r, fill_color.g, fill_color.b));
}

void CairoOutputDev::updateStrokeColor(GfxState *state) {
  state->getStrokeRGB(&stroke_color);

  cairo_pattern_destroy(stroke_pattern);
  stroke_pattern = cairo_pattern_create_rgba(stroke_color.r / 65535.0,
					     stroke_color.g / 65535.0,
					     stroke_color.b / 65535.0,
					     stroke_opacity);
  
  LOG(printf ("stroke color: %d %d %d\n",
	      stroke_color.r, stroke_color.g, stroke_color.b));
}

void CairoOutputDev::updateFillOpacity(GfxState *state) {
  fill_opacity = state->getFillOpacity();

  cairo_pattern_destroy(fill_pattern);
  fill_pattern = cairo_pattern_create_rgba(fill_color.r / 65535.0,
					   fill_color.g / 65535.0,
					   fill_color.b / 65535.0,
					   fill_opacity);

  LOG(printf ("fill opacity: %f\n", fill_opacity));
}

void CairoOutputDev::updateStrokeOpacity(GfxState *state) {
  stroke_opacity = state->getStrokeOpacity();

  cairo_pattern_destroy(stroke_pattern);
  stroke_pattern = cairo_pattern_create_rgba(stroke_color.r / 65535.0,
					     stroke_color.g / 65535.0,
					     stroke_color.b / 65535.0,
					     stroke_opacity);
  
  LOG(printf ("stroke opacity: %f\n", stroke_opacity));
}

void CairoOutputDev::updateFont(GfxState *state) {
  cairo_font_face_t *font_face;
  cairo_matrix_t matrix;

  LOG(printf ("updateFont() font=%s\n", state->getFont()->getName()->getCString()));

  needFontUpdate = gFalse;

  if (state->getFont()->getType() == fontType3)	 
    return;

  currentFont = fontEngine->getFont (state->getFont(), xref);

  if (!currentFont)
    return;

  LOG(printf ("font matrix: %f %f %f %f\n", m11, m12, m21, m22));
  
  font_face = currentFont->getFontFace();
  cairo_set_font_face (cairo, font_face);
 
  double fontSize = state->getFontSize();
  double *m = state->getTextMat();
  matrix.xx = m[0] * fontSize * state->getHorizScaling();
  matrix.yx = m[1] * fontSize * state->getHorizScaling();
  matrix.xy = -m[2] * fontSize;
  matrix.yy = -m[3] * fontSize;
  matrix.x0 = 0;
  matrix.y0 = 0;
  cairo_set_font_matrix (cairo, &matrix);
}

void CairoOutputDev::doPath(GfxState *state, GfxPath *path) {
  GfxSubpath *subpath;
  int i, j;
  for (i = 0; i < path->getNumSubpaths(); ++i) {
    subpath = path->getSubpath(i);
    if (subpath->getNumPoints() > 0) {
      cairo_move_to (cairo, subpath->getX(0), subpath->getY(0));
         j = 1;
      while (j < subpath->getNumPoints()) {
	if (subpath->getCurve(j)) {
	  cairo_curve_to( cairo,
			  subpath->getX(j), subpath->getY(j),
			  subpath->getX(j+1), subpath->getY(j+1),
			  subpath->getX(j+2), subpath->getY(j+2));

	  j += 3;
	} else {
	  cairo_line_to (cairo, subpath->getX(j), subpath->getY(j));
	  ++j;
	}
      }
      if (subpath->isClosed()) {
	LOG (printf ("close\n"));
	cairo_close_path (cairo);
      }
    }
  }
}

void CairoOutputDev::stroke(GfxState *state) {
  doPath (state, state->getPath());
  cairo_set_source (cairo, stroke_pattern);
  LOG(printf ("stroke\n"));
  cairo_stroke (cairo);
}

void CairoOutputDev::fill(GfxState *state) {
  doPath (state, state->getPath());
  cairo_set_fill_rule (cairo, CAIRO_FILL_RULE_WINDING);
  cairo_set_source (cairo, fill_pattern);
  LOG(printf ("fill\n"));
  cairo_fill (cairo);
}

void CairoOutputDev::eoFill(GfxState *state) {
  doPath (state, state->getPath());
  cairo_set_fill_rule (cairo, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_set_source (cairo, fill_pattern);
  LOG(printf ("fill-eo\n"));
  cairo_fill (cairo);
}

void CairoOutputDev::clip(GfxState *state) {
  doPath (state, state->getPath());
  cairo_set_fill_rule (cairo, CAIRO_FILL_RULE_WINDING);
  cairo_clip (cairo);
  LOG (printf ("clip\n"));
}

void CairoOutputDev::eoClip(GfxState *state) {
  doPath (state, state->getPath());
  cairo_set_fill_rule (cairo, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_clip (cairo);
  LOG (printf ("clip-eo\n"));
}

void CairoOutputDev::beginString(GfxState *state, GooString *s)
{
  int len = s->getLength();

  if (needFontUpdate)
    updateFont(state);

  if (!currentFont)
    return;

  glyphs = (cairo_glyph_t *) gmalloc (len * sizeof (cairo_glyph_t));
  glyphCount = 0;
}

void CairoOutputDev::drawChar(GfxState *state, double x, double y,
			      double dx, double dy,
			      double originX, double originY,
			      CharCode code, int nBytes, Unicode *u, int uLen)
{
  if (!currentFont)
    return;
  
  glyphs[glyphCount].index = currentFont->getGlyph (code, u, uLen);
  glyphs[glyphCount].x = x - originX;
  glyphs[glyphCount].y = y - originY;
  glyphCount++;
}

void CairoOutputDev::endString(GfxState *state)
{
  int render;

  if (!currentFont)
    return;

  // endString can be called without a corresponding beginString. If this
  // happens glyphs will be null so don't draw anything, just return.
  // XXX: OutputDevs should probably not have to deal with this...
  if (!glyphs)
    return;

  // ignore empty strings and invisible text -- this is used by
  // Acrobat Capture
  render = state->getRender();
  if (render == 3 || glyphCount == 0) {
    gfree(glyphs);
    glyphs = NULL;
    return;
  }
  
  if (!(render & 1)) {
    LOG (printf ("fill string\n"));
    cairo_set_source (cairo, fill_pattern);
    cairo_show_glyphs (cairo, glyphs, glyphCount);
  }
  
  // stroke
  if ((render & 3) == 1 || (render & 3) == 2) {
    LOG (printf ("stroke string\n"));
    cairo_set_source (cairo, stroke_pattern);
    cairo_glyph_path (cairo, glyphs, glyphCount);
    cairo_stroke (cairo);
  }

  // clip
  if (render & 4) {
    LOG (printf ("clip string\n"));
    // append the glyph path to textClipPath.

    // set textClipPath as the currentPath
    if (textClipPath) {
      cairo_append_path (cairo, textClipPath);
      cairo_path_destroy (textClipPath);
    }
    
    // append the glyph path
    cairo_glyph_path (cairo, glyphs, glyphCount);
   
    // move the path back into textClipPath 
    // and clear the current path
    textClipPath = cairo_copy_path (cairo);
    cairo_new_path (cairo);
  }

  gfree (glyphs);
  glyphs = NULL;
}


GBool CairoOutputDev::beginType3Char(GfxState *state, double x, double y,
				      double dx, double dy,
				      CharCode code, Unicode *u, int uLen) {

  cairo_save (cairo);
  double *ctm;
  cairo_matrix_t matrix;

  ctm = state->getCTM();
  matrix.xx = ctm[0];
  matrix.yx = ctm[1];
  matrix.xy = ctm[2];
  matrix.yy = ctm[3];
  matrix.x0 = ctm[4];
  matrix.y0 = ctm[5];
  /* Restore the original matrix and then transform to matrix needed for the
   * type3 font. This is ugly but seems to work. Perhaps there is a better way to do it?*/
  cairo_set_matrix(cairo, &orig_matrix);
  cairo_transform(cairo, &matrix);
  return gFalse;
}

void CairoOutputDev::endType3Char(GfxState *state) {
  cairo_restore (cairo);
}

void CairoOutputDev::type3D0(GfxState *state, double wx, double wy) {
}

void CairoOutputDev::type3D1(GfxState *state, double wx, double wy,
			     double llx, double lly, double urx, double ury) {
}

void CairoOutputDev::endTextObject(GfxState *state) {
  if (textClipPath) {
    // clip the accumulated text path
    cairo_append_path (cairo, textClipPath);
    cairo_clip (cairo);
    cairo_path_destroy (textClipPath);
    textClipPath = NULL;
  }

}

static inline int splashRound(SplashCoord x) {
  return (int)floor(x + 0.5);
}

static inline int splashCeil(SplashCoord x) {
  return (int)ceil(x);
}

static inline int splashFloor(SplashCoord x) {
  return (int)floor(x);
}

void CairoOutputDev::beginTransparencyGroup(GfxState * /*state*/, double * /*bbox*/,
                                      GfxColorSpace * blendingColorSpace,
                                      GBool /*isolated*/, GBool /*knockout*/,
				      GBool forSoftMask) {
  /* push color space */
  ColorSpaceStack* css = new ColorSpaceStack;
  css->cs = blendingColorSpace;
  css->next = groupColorSpaceStack;
  groupColorSpaceStack = css;

  if (0 && forSoftMask)
    cairo_push_group_with_content (cairo, CAIRO_CONTENT_ALPHA);
  else
    cairo_push_group (cairo);
}
void CairoOutputDev::endTransparencyGroup(GfxState * /*state*/) {
  if (group)
    cairo_pattern_destroy(group);
  group = cairo_pop_group (cairo);
}
void CairoOutputDev::paintTransparencyGroup(GfxState * /*state*/, double * /*bbox*/) {
  cairo_set_source (cairo, group);

  if (!mask) {
    cairo_paint_with_alpha (cairo, fill_opacity);
  } else {
    cairo_mask(cairo, mask);

    cairo_pattern_destroy(mask);
    mask = NULL;
  }

  /* pop color space */
  ColorSpaceStack *css = groupColorSpaceStack;
  groupColorSpaceStack = css->next;
  delete css;
}

typedef unsigned int uint32_t;

static uint32_t luminocity(uint32_t x)
{
  int r = (x >> 16) & 0xff;
  int g = (x >>  8) & 0xff;
  int b = (x >>  0) & 0xff;
  int y = (int) (0.3 * r + 0.59 * g + 0.11 * b);
  return y << 24;
}


void CairoOutputDev::setSoftMask(GfxState * state, double * bbox, GBool alpha,
                                 Function * transferFunc, GfxColor * backdropColor) {
  if (alpha == false) {
    /* We need to mask according to the luminocity of the group.
     * So we paint the group to an image surface convert it to a luminocity map
     * and then use that as the mask. */

    double x1, y1, x2, y2;
    cairo_clip_extents(cairo, &x1, &y1, &x2, &y2);
    int width = (int)(ceil(x2) - floor(x1));
    int height = (int)(ceil(y2) - floor(y1));

    cairo_surface_t *source = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *maskCtx = cairo_create(source);

    //XXX: hopefully this uses the correct color space */
    GfxRGB backdropColorRGB;
    groupColorSpaceStack->cs->getRGB(backdropColor, &backdropColorRGB);
    /* paint the backdrop */
    cairo_set_source_rgb(maskCtx, backdropColorRGB.r / 65535.0,
			 backdropColorRGB.g / 65535.0,
			 backdropColorRGB.b / 65535.0);


    cairo_matrix_t mat;
    cairo_get_matrix(cairo, &mat);
    cairo_set_matrix(maskCtx, &mat);

    /* make the device offset of the new mask match that of the group */
    double x_offset, y_offset;
    cairo_surface_t *pats;
    cairo_pattern_get_surface(group, &pats);
    cairo_surface_get_device_offset(pats, &x_offset, &y_offset);
    cairo_surface_set_device_offset(source, x_offset, y_offset);

    /* paint the group */
    cairo_set_source(maskCtx, group);
    cairo_paint(maskCtx);

    /* convert to a luminocity map */
    uint32_t *source_data = (uint32_t*)cairo_image_surface_get_data(source);
    /* get stride in units of 32 bits */
    int stride = cairo_image_surface_get_stride(source)/4;
    for (int y=0; y<height; y++) {
      for (int x=0; x<width; x++) {
	source_data[y*stride + x] = luminocity(source_data[y*stride + x]);

#if 0
	here is how splash deals with the transferfunction we should deal with this
	  at some point
	if (transferFunc) {
	  transferFunc->transform(&lum, &lum2);
	} else {
	  lum2 = lum;
	}
	p[x] = (int)(lum2 * 255.0 + 0.5);
#endif

      }
    }

    /* setup the new mask pattern */
    mask = cairo_pattern_create_for_surface(source);
    cairo_matrix_t patMatrix;
    cairo_pattern_get_matrix(group, &patMatrix);
    cairo_pattern_set_matrix(mask, &patMatrix);

    cairo_surface_destroy(source);
    cairo_surface_destroy(pats);
  } else {
    cairo_pattern_reference(group);
    mask = group;
  }
}

void CairoOutputDev::clearSoftMask(GfxState * /*state*/) {
  //XXX: should we be doing anything here?
}

void CairoOutputDev::drawImageMask(GfxState *state, Object *ref, Stream *str,
				    int width, int height, GBool invert,
				    GBool inlineImg) {

  /* FIXME: Doesn't the image mask support any colorspace? */
  cairo_set_source (cairo, fill_pattern);

  /* work around a cairo bug when scaling 1x1 surfaces */
  if (width == 1 && height == 1) {
    cairo_save (cairo);
    cairo_rectangle (cairo, 0., 0., width, height);
    cairo_fill (cairo);
    cairo_restore (cairo);
    return;
  }

  cairo_matrix_t matrix;
  cairo_get_matrix (cairo, &matrix);
  if (prescaleImages && matrix.xy == 0.0 && matrix.yx == 0.0) {
    drawImageMaskPrescaled(state, ref, str, width, height, invert, inlineImg);
  } else {
    drawImageMaskRegular(state, ref, str, width, height, invert, inlineImg);
  }
}

void CairoOutputDev::drawImageMaskRegular(GfxState *state, Object *ref, Stream *str,
				    int width, int height, GBool invert,
				    GBool inlineImg) {
  unsigned char *buffer;
  unsigned char *dest;
  cairo_surface_t *image;
  cairo_pattern_t *pattern;
  int x, y;
  ImageStream *imgStr;
  Guchar *pix;
  cairo_matrix_t matrix;
  int invert_bit;
  int row_stride;

  row_stride = (width + 3) & ~3;
  buffer = (unsigned char *) malloc (height * row_stride);
  if (buffer == NULL) {
    error(-1, "Unable to allocate memory for image.");
    return;
  }

  /* TODO: Do we want to cache these? */
  imgStr = new ImageStream(str, width, 1, 1);
  imgStr->reset();

  invert_bit = invert ? 1 : 0;

  for (y = 0; y < height; y++) {
    pix = imgStr->getLine();
    dest = buffer + y * row_stride;
    for (x = 0; x < width; x++) {

      if (pix[x] ^ invert_bit)
	*dest++ = 0;
      else
	*dest++ = 255;
    }
  }

  image = cairo_image_surface_create_for_data (buffer, CAIRO_FORMAT_A8,
					       width, height, row_stride);
  if (image == NULL) {
    delete imgStr;
    return;
  }
  pattern = cairo_pattern_create_for_surface (image);
  if (pattern == NULL) {
    delete imgStr;
    return;
  }

  cairo_matrix_init_translate (&matrix, 0, height);
  cairo_matrix_scale (&matrix, width, -height);

  cairo_pattern_set_matrix (pattern, &matrix);

  /* we should actually be using CAIRO_FILTER_NEAREST here. However,
   * cairo doesn't yet do minifaction filtering causing scaled down
   * images with CAIRO_FILTER_NEAREST to look really bad */
  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BEST);

  cairo_mask (cairo, pattern);

  cairo_pattern_destroy (pattern);
  cairo_surface_destroy (image);
  free (buffer);
  delete imgStr;
}


void CairoOutputDev::drawImageMaskPrescaled(GfxState *state, Object *ref, Stream *str,
				    int width, int height, GBool invert,
				    GBool inlineImg) {
  unsigned char *buffer;
  cairo_surface_t *image;
  cairo_pattern_t *pattern;
  ImageStream *imgStr;
  Guchar *pix;
  cairo_matrix_t matrix;
  int invert_bit;
  int row_stride;

  /* cairo does a very poor job of scaling down images so we scale them ourselves */

  /* this scaling code is adopted from the splash image scaling code */
  cairo_get_matrix(cairo, &matrix);
#if 0
  printf("[%f %f], [%f %f], %f %f\n", matrix.xx, matrix.xy, matrix.yx, matrix.yy, matrix.x0, matrix.y0);
#endif
  /* this whole computation should be factored out */
  double xScale = matrix.xx;
  double yScale = matrix.yy;
  int tx, tx2, ty, ty2; /* the integer co-oridinates of the resulting image */
  int scaledHeight;
  int scaledWidth;
  if (xScale >= 0) {
    tx = splashRound(matrix.x0 - 0.01);
    tx2 = splashRound(matrix.x0 + xScale + 0.01) - 1;
  } else {
    tx = splashRound(matrix.x0 + 0.01) - 1;
    tx2 = splashRound(matrix.x0 + xScale - 0.01);
  }
  scaledWidth = abs(tx2 - tx) + 1;
  //scaledWidth = splashRound(fabs(xScale));
  if (scaledWidth == 0) {
    // technically, this should draw nothing, but it generally seems
    // better to draw a one-pixel-wide stripe rather than throwing it
    // away
    scaledWidth = 1;
  }
  if (yScale >= 0) {
    ty = splashFloor(matrix.y0 + 0.01);
    ty2 = splashCeil(matrix.y0 + yScale - 0.01);
  } else {
    ty = splashCeil(matrix.y0 - 0.01);
    ty2 = splashFloor(matrix.y0 + yScale + 0.01);
  }
  scaledHeight = abs(ty2 - ty);
  if (scaledHeight == 0) {
    scaledHeight = 1;
  }
#if 0
  printf("xscale: %g, yscale: %g\n", xScale, yScale);
  printf("width: %d, height: %d\n", width, height);
  printf("scaledWidth: %d, scaledHeight: %d\n", scaledWidth, scaledHeight);
#endif

  /* compute the required padding */
  /* Padding is used to preserve the aspect ratio.
     We compute total_pad to make (height+total_pad)/scaledHeight as close to height/yScale as possible */
  int head_pad = 0;
  int tail_pad = 0;
  int total_pad = splashRound(height*(scaledHeight/fabs(yScale)) - height);

  /* compute the two pieces of padding */
  if (total_pad > 0) {
    //XXX: i'm not positive fabs() is correct
    float tail_error = fabs(matrix.y0 - ty);
    float head_error = fabs(ty2 - (matrix.y0 + yScale));
    float tail_fraction = tail_error/(tail_error + head_error);
    tail_pad = splashRound(total_pad*tail_fraction);
    head_pad = total_pad - tail_pad;
  } else {
    tail_pad = 0;
    head_pad = 0;
  }
  int origHeight = height;
  height += tail_pad;
  height += head_pad;
#if 0
  printf("head_pad: %d tail_pad: %d\n", head_pad, tail_pad);
  printf("origHeight: %d height: %d\n", origHeight, height);
  printf("ty: %d, ty2: %d\n", ty, ty2);
#endif

  /* TODO: Do we want to cache these? */
  imgStr = new ImageStream(str, width, 1, 1);
  imgStr->reset();

  invert_bit = invert ? 1 : 0;

  row_stride = (scaledWidth + 3) & ~3;
  buffer = (unsigned char *) malloc (scaledHeight * row_stride);
  if (buffer == NULL) {
    error(-1, "Unable to allocate memory for image.");
    return;
  }

  int yp = height / scaledHeight;
  int yq = height % scaledHeight;
  int xp = width / scaledWidth;
  int xq = width % scaledWidth;
  int yt = 0;
  int origHeight_c = origHeight;
  /* use MIN() because yp might be > origHeight because of padding */
  unsigned char *pixBuf = (unsigned char *)malloc(MIN(yp+1, origHeight)*width);
  int lastYStep = 1;
  int total = 0;
  for (int y = 0; y < scaledHeight; y++) {
    // y scale Bresenham
    int yStep = yp;
    yt += yq;

    if (yt >= scaledHeight) {
      yt -= scaledHeight;
      ++yStep;
    }

    // read row (s) from image ignoring the padding as appropriate
    {
      int n = (yp > 0) ? yStep : lastYStep;
      total += n;
      if (n > 0) {
	unsigned char *p = pixBuf;
	int head_pad_count = head_pad;
	int origHeight_count = origHeight;
	int tail_pad_count = tail_pad;
	for (int i=0; i<n; i++) {
	  // get row
	  if (head_pad_count) {
	    head_pad_count--;
	  } else if (origHeight_count) {
	    pix = imgStr->getLine();
	    for (int j=0; j<width; j++) {
	      if (pix[j] ^ invert_bit)
		p[j] = 0;
	      else
		p[j] = 255;
	    }
	    origHeight_count--;
	    p += width;
	  } else if (tail_pad_count) {
	    tail_pad_count--;
	  } else {
	    printf("%d %d\n", n, total);
	    assert(0 && "over run\n");
	  }
	}
      }
    }

    lastYStep = yStep;
    int k1 = y;

    int xt = 0;
    int xSrc = 0;
    int x1 = k1;
    int n = yStep > 0 ? yStep : 1;
    int origN = n;

    /* compute the size of padding and pixels that will be used for this row */
    int head_pad_size = MIN(n, head_pad);
    n -= head_pad_size;
    head_pad -= MIN(head_pad_size, yStep);

    int pix_size = MIN(n, origHeight);
    n -= pix_size;
    origHeight -= MIN(pix_size, yStep);

    int tail_pad_size = MIN(n, tail_pad);
    n -= tail_pad_size;
    tail_pad -= MIN(tail_pad_size, yStep);
    if (n != 0) {
      printf("n = %d (%d %d %d)\n", n, head_pad_size, pix_size, tail_pad_size);
      assert(n == 0);
    }

    for (int x = 0; x < scaledWidth; ++x) {
      int xStep = xp;
      xt += xq;
      if (xt >= scaledWidth) {
	xt -= scaledWidth;
	++xStep;
      }
      int m = xStep > 0 ? xStep : 1;
      float pixAcc0 = 0;
      /* could m * head_pad_size * tail_pad_size  overflow? */
      if (invert_bit) {
	pixAcc0 += m * head_pad_size * tail_pad_size * 255;
      } else {
	pixAcc0 += m * head_pad_size * tail_pad_size * 0;
      }
      /* Accumulate all of the source pixels for the destination pixel */
      for (int i = 0; i < pix_size; ++i) {
	for (int j = 0; j< m; ++j) {
	  if (xSrc + i*width + j > MIN(yp + 1, origHeight_c)*width) {
	    printf("%d > %d (%d %d %d %d) (%d %d %d)\n", xSrc + i*width + j, MIN(yp + 1, origHeight_c)*width, xSrc, i , width, j, yp, origHeight_c, width);
	    printf("%d %d %d\n", head_pad_size, pix_size, tail_pad_size);
	    assert(0 && "bad access\n");
	  }
	  pixAcc0 += pixBuf[xSrc + i*width + j];
	}
      }
      buffer[y * row_stride + x] = splashFloor(pixAcc0 / (origN*m));
      xSrc += xStep;
      x1 += 1;
    }

  }
  free(pixBuf);

  //XXX: we should handle error's better than this
  image = cairo_image_surface_create_for_data (buffer, CAIRO_FORMAT_A8,
      scaledWidth, scaledHeight, row_stride);
  if (image == NULL) {
    delete imgStr;
    return;
  }
  pattern = cairo_pattern_create_for_surface (image);
  if (pattern == NULL) {
    delete imgStr;
    return;
  }

  /* we should actually be using CAIRO_FILTER_NEAREST here. However,
   * cairo doesn't yet do minifaction filtering causing scaled down
   * images with CAIRO_FILTER_NEAREST to look really bad */
  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BEST);

  cairo_save (cairo);

  /* modify our current transformation so that the prescaled image
   * goes where it is supposed to */
  cairo_get_matrix(cairo, &matrix);
  cairo_scale(cairo, 1.0/matrix.xx, 1.0/matrix.yy);
  // get integer co-ords
  cairo_translate (cairo, tx - matrix.x0, ty2 - matrix.y0);
  if (yScale > 0)
    cairo_scale(cairo, 1, -1);

  cairo_mask (cairo, pattern);

  //cairo_get_matrix(cairo, &matrix);
  //printf("mask at: [%f %f], [%f %f], %f %f\n\n", matrix.xx, matrix.xy, matrix.yx, matrix.yy, matrix.x0, matrix.y0);
  cairo_restore(cairo);

  cairo_pattern_destroy (pattern);
  cairo_surface_destroy (image);
  free (buffer);
  delete imgStr;
}

void CairoOutputDev::drawMaskedImage(GfxState *state, Object *ref,
				Stream *str, int width, int height,
				GfxImageColorMap *colorMap,
				Stream *maskStr, int maskWidth,
				int maskHeight, GBool maskInvert)
{
  ImageStream *maskImgStr;
  maskImgStr = new ImageStream(maskStr, maskWidth, 1, 1);
  maskImgStr->reset();

  int row_stride = (maskWidth + 3) & ~3;
  unsigned char *maskBuffer;
  maskBuffer = (unsigned char *)gmalloc (row_stride * maskHeight);
  unsigned char *maskDest;
  cairo_surface_t *maskImage;
  cairo_pattern_t *maskPattern;
  Guchar *pix;
  int x, y;

  int invert_bit;
  
  invert_bit = maskInvert ? 1 : 0;

  for (y = 0; y < height; y++) {
    pix = maskImgStr->getLine();
    maskDest = maskBuffer + y * row_stride;
    for (x = 0; x < width; x++) {
      if (pix[x] ^ invert_bit)
	*maskDest++ = 0;
      else
	*maskDest++ = 255;
    }
  }

  maskImage = cairo_image_surface_create_for_data (maskBuffer, CAIRO_FORMAT_A8,
						 maskWidth, maskHeight, row_stride);

  delete maskImgStr;
  maskStr->close();

  unsigned char *buffer;
  unsigned int *dest;
  cairo_surface_t *image;
  cairo_pattern_t *pattern;
  ImageStream *imgStr;
  cairo_matrix_t matrix;
  int is_identity_transform;

  buffer = (unsigned char *)gmalloc (width * height * 4);

  /* TODO: Do we want to cache these? */
  imgStr = new ImageStream(str, width,
			   colorMap->getNumPixelComps(),
			   colorMap->getBits());
  imgStr->reset();
  
  /* ICCBased color space doesn't do any color correction
   * so check its underlying color space as well */
  is_identity_transform = colorMap->getColorSpace()->getMode() == csDeviceRGB ||
		  colorMap->getColorSpace()->getMode() == csICCBased && 
		  ((GfxICCBasedColorSpace*)colorMap->getColorSpace())->getAlt()->getMode() == csDeviceRGB;

  for (y = 0; y < height; y++) {
    dest = (unsigned int *) (buffer + y * 4 * width);
    pix = imgStr->getLine();
    colorMap->getRGBLine (pix, dest, width);
  }

  image = cairo_image_surface_create_for_data (buffer, CAIRO_FORMAT_RGB24,
						 width, height, width * 4);

  if (image == NULL) {
    delete imgStr;
    return;
  }
  pattern = cairo_pattern_create_for_surface (image);
  maskPattern = cairo_pattern_create_for_surface (maskImage);
  if (pattern == NULL) {
    delete imgStr;
    return;
  }

  LOG (printf ("drawMaskedImage %dx%d\n", width, height));

  cairo_matrix_init_translate (&matrix, 0, height);
  cairo_matrix_scale (&matrix, width, -height);

  /* scale the mask to the size of the image unlike softMask */
  cairo_pattern_set_matrix (pattern, &matrix);
  cairo_pattern_set_matrix (maskPattern, &matrix);

  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BILINEAR);
  cairo_set_source (cairo, pattern);
  cairo_mask (cairo, maskPattern);

  cairo_pattern_destroy (maskPattern);
  cairo_surface_destroy (maskImage);
  cairo_pattern_destroy (pattern);
  cairo_surface_destroy (image);
  free (buffer);
  free (maskBuffer);
  delete imgStr;
}

void CairoOutputDev::drawSoftMaskedImage(GfxState *state, Object *ref, Stream *str,
				int width, int height,
				GfxImageColorMap *colorMap,
				Stream *maskStr,
				int maskWidth, int maskHeight,
				GfxImageColorMap *maskColorMap)
{
  ImageStream *maskImgStr;
  maskImgStr = new ImageStream(maskStr, maskWidth,
				       maskColorMap->getNumPixelComps(),
				       maskColorMap->getBits());
  maskImgStr->reset();

  int row_stride = (maskWidth + 3) & ~3;
  unsigned char *maskBuffer;
  maskBuffer = (unsigned char *)gmalloc (row_stride * maskHeight);
  unsigned char *maskDest;
  cairo_surface_t *maskImage;
  cairo_pattern_t *maskPattern;
  Guchar *pix;
  int y;
  for (y = 0; y < maskHeight; y++) {
    maskDest = (unsigned char *) (maskBuffer + y * row_stride);
    pix = maskImgStr->getLine();
    maskColorMap->getGrayLine (pix, maskDest, maskWidth);
  }

  maskImage = cairo_image_surface_create_for_data (maskBuffer, CAIRO_FORMAT_A8,
						 maskWidth, maskHeight, row_stride);

  delete maskImgStr;
  maskStr->close();

  unsigned char *buffer;
  unsigned int *dest;
  cairo_surface_t *image;
  cairo_pattern_t *pattern;
  ImageStream *imgStr;
  cairo_matrix_t matrix;
  cairo_matrix_t maskMatrix;
  int is_identity_transform;

  buffer = (unsigned char *)gmalloc (width * height * 4);

  /* TODO: Do we want to cache these? */
  imgStr = new ImageStream(str, width,
			   colorMap->getNumPixelComps(),
			   colorMap->getBits());
  imgStr->reset();
  
  /* ICCBased color space doesn't do any color correction
   * so check its underlying color space as well */
  is_identity_transform = colorMap->getColorSpace()->getMode() == csDeviceRGB ||
		  colorMap->getColorSpace()->getMode() == csICCBased && 
		  ((GfxICCBasedColorSpace*)colorMap->getColorSpace())->getAlt()->getMode() == csDeviceRGB;

  for (y = 0; y < height; y++) {
    dest = (unsigned int *) (buffer + y * 4 * width);
    pix = imgStr->getLine();
    colorMap->getRGBLine (pix, dest, width);
  }

  image = cairo_image_surface_create_for_data (buffer, CAIRO_FORMAT_RGB24,
						 width, height, width * 4);

  if (image == NULL) {
    delete imgStr;
    return;
  }
  pattern = cairo_pattern_create_for_surface (image);
  maskPattern = cairo_pattern_create_for_surface (maskImage);
  if (pattern == NULL) {
    delete imgStr;
    return;
  }

  LOG (printf ("drawSoftMaskedImage %dx%d\n", width, height));

  cairo_matrix_init_translate (&matrix, 0, height);
  cairo_matrix_scale (&matrix, width, -height);

  cairo_matrix_init_translate (&maskMatrix, 0, maskHeight);
  cairo_matrix_scale (&maskMatrix, maskWidth, -maskHeight);

  cairo_pattern_set_matrix (pattern, &matrix);
  cairo_pattern_set_matrix (maskPattern, &maskMatrix);

  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BILINEAR);
  cairo_set_source (cairo, pattern);
  cairo_mask (cairo, maskPattern);

  cairo_pattern_destroy (maskPattern);
  cairo_surface_destroy (maskImage);
  cairo_pattern_destroy (pattern);
  cairo_surface_destroy (image);
  free (buffer);
  free (maskBuffer);
  delete imgStr;
}
void CairoOutputDev::drawImage(GfxState *state, Object *ref, Stream *str,
				int width, int height,
				GfxImageColorMap *colorMap,
				int *maskColors, GBool inlineImg)
{
  unsigned char *buffer;
  unsigned int *dest;
  cairo_surface_t *image;
  cairo_pattern_t *pattern;
  int x, y;
  ImageStream *imgStr;
  Guchar *pix;
  int i;
  cairo_matrix_t matrix;
  int is_identity_transform;
  
  buffer = (unsigned char *)gmalloc (width * height * 4);

  /* TODO: Do we want to cache these? */
  imgStr = new ImageStream(str, width,
			   colorMap->getNumPixelComps(),
			   colorMap->getBits());
  imgStr->reset();
  
  /* ICCBased color space doesn't do any color correction
   * so check its underlying color space as well */
  is_identity_transform = colorMap->getColorSpace()->getMode() == csDeviceRGB ||
		  colorMap->getColorSpace()->getMode() == csICCBased && 
		  ((GfxICCBasedColorSpace*)colorMap->getColorSpace())->getAlt()->getMode() == csDeviceRGB;

  if (maskColors) {
    for (y = 0; y < height; y++) {
      dest = (unsigned int *) (buffer + y * 4 * width);
      pix = imgStr->getLine();
      colorMap->getRGBLine (pix, dest, width);

      for (x = 0; x < width; x++) {
	for (i = 0; i < colorMap->getNumPixelComps(); ++i) {
	  
	  if (pix[i] < maskColors[2*i] * 255||
	      pix[i] > maskColors[2*i+1] * 255) {
	    *dest = *dest | 0xff000000;
	    break;
	  }
	}
	pix += colorMap->getNumPixelComps();
	dest++;
      }
    }

    image = cairo_image_surface_create_for_data (buffer, CAIRO_FORMAT_ARGB32,
						 width, height, width * 4);
  }
  else {
    for (y = 0; y < height; y++) {
      dest = (unsigned int *) (buffer + y * 4 * width);
      pix = imgStr->getLine();
      colorMap->getRGBLine (pix, dest, width);
    }

    image = cairo_image_surface_create_for_data (buffer, CAIRO_FORMAT_RGB24,
						 width, height, width * 4);
  }

  if (image == NULL) {
   delete imgStr;
   return;
  }
  pattern = cairo_pattern_create_for_surface (image);
  if (pattern == NULL) {
    delete imgStr;
    return;
  }

  LOG (printf ("drawImageMask %dx%d\n", width, height));
  
  cairo_matrix_init_translate (&matrix, 0, height);
  cairo_matrix_scale (&matrix, width, -height);

  cairo_pattern_set_matrix (pattern, &matrix);

  cairo_pattern_set_filter (pattern, CAIRO_FILTER_BILINEAR);
  cairo_set_source (cairo, pattern);
  cairo_paint (cairo);

  cairo_pattern_destroy (pattern);
  cairo_surface_destroy (image);
  free (buffer);
  delete imgStr;
}


//------------------------------------------------------------------------
// ImageOutputDev
//------------------------------------------------------------------------

CairoImageOutputDev::CairoImageOutputDev()
{
  images = NULL;
  numImages = 0;
  size = 0;
}

CairoImageOutputDev::~CairoImageOutputDev()
{
  int i;

  for (i = 0; i < numImages; i++)
    delete images[i];
  gfree (images);
}

void CairoImageOutputDev::saveImage(CairoImage *image)
{ 
  if (numImages >= size) {
	  size += 16;
	  images = (CairoImage **) greallocn (images, size, sizeof (CairoImage *));
  }
  images[numImages++] = image;
}	

void CairoImageOutputDev::drawImageMask(GfxState *state, Object *ref, Stream *str,
					int width, int height, GBool invert,
					GBool inlineImg)
{
  cairo_t *cr;
  cairo_surface_t *surface;
  double x1, y1, x2, y2;
  double *ctm;
  double mat[6];
  CairoImage *image;

  ctm = state->getCTM();
  
  mat[0] = ctm[0];
  mat[1] = ctm[1];
  mat[2] = -ctm[2];
  mat[3] = -ctm[3];
  mat[4] = ctm[2] + ctm[4];
  mat[5] = ctm[3] + ctm[5];
  x1 = mat[4];
  y1 = mat[5];
  x2 = x1 + width;
  y2 = y1 + height;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);
  setCairo (cr);
  cairo_translate (cr, 0, height);
  cairo_scale (cr, width, -height);

  CairoOutputDev::drawImageMask(state, ref, str, width, height, invert, inlineImg);

  image = new CairoImage (surface, x1, y1, x2, y2);
  saveImage (image);
  
  setCairo (NULL);
  cairo_surface_destroy (surface);
  cairo_destroy (cr);
}

void CairoImageOutputDev::drawImage(GfxState *state, Object *ref, Stream *str,
				    int width, int height, GfxImageColorMap *colorMap,
				    int *maskColors, GBool inlineImg)
{
  cairo_t *cr;
  cairo_surface_t *surface;
  double x1, y1, x2, y2;
  double *ctm;
  double mat[6];
  CairoImage *image;

  ctm = state->getCTM();
  
  mat[0] = ctm[0];
  mat[1] = ctm[1];
  mat[2] = -ctm[2];
  mat[3] = -ctm[3];
  mat[4] = ctm[2] + ctm[4];
  mat[5] = ctm[3] + ctm[5];
  x1 = mat[4];
  y1 = mat[5];
  x2 = x1 + width;
  y2 = y1 + height;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);
  setCairo (cr);
  cairo_translate (cr, 0, height);
  cairo_scale (cr, width, -height);

  CairoOutputDev::drawImage(state, ref, str, width, height, colorMap, maskColors, inlineImg);

  image = new CairoImage (surface, x1, y1, x2, y2);
  saveImage (image);
  
  setCairo (NULL);
  cairo_surface_destroy (surface);
  cairo_destroy (cr);
}

void CairoImageOutputDev::drawSoftMaskedImage(GfxState *state, Object *ref, Stream *str,
					      int width, int height,
					      GfxImageColorMap *colorMap,
					      Stream *maskStr,
					      int maskWidth, int maskHeight,
					      GfxImageColorMap *maskColorMap)
{
  cairo_t *cr;
  cairo_surface_t *surface;
  double x1, y1, x2, y2;
  double *ctm;
  double mat[6];
  CairoImage *image;

  ctm = state->getCTM();
  
  mat[0] = ctm[0];
  mat[1] = ctm[1];
  mat[2] = -ctm[2];
  mat[3] = -ctm[3];
  mat[4] = ctm[2] + ctm[4];
  mat[5] = ctm[3] + ctm[5];
  x1 = mat[4];
  y1 = mat[5];
  x2 = x1 + width;
  y2 = y1 + height;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);
  setCairo (cr);
  cairo_translate (cr, 0, height);
  cairo_scale (cr, width, -height);

  CairoOutputDev::drawSoftMaskedImage(state, ref, str, width, height, colorMap,
				      maskStr, maskWidth, maskHeight, maskColorMap);

  image = new CairoImage (surface, x1, y1, x2, y2);
  saveImage (image);
  
  setCairo (NULL);
  cairo_surface_destroy (surface);
  cairo_destroy (cr);
}

void CairoImageOutputDev::drawMaskedImage(GfxState *state, Object *ref, Stream *str,
					  int width, int height,
					  GfxImageColorMap *colorMap,
					  Stream *maskStr,
					  int maskWidth, int maskHeight,
					  GBool maskInvert)
{
  cairo_t *cr;
  cairo_surface_t *surface;
  double x1, y1, x2, y2;
  double *ctm;
  double mat[6];
  CairoImage *image;

  ctm = state->getCTM();
  
  mat[0] = ctm[0];
  mat[1] = ctm[1];
  mat[2] = -ctm[2];
  mat[3] = -ctm[3];
  mat[4] = ctm[2] + ctm[4];
  mat[5] = ctm[3] + ctm[5];
  x1 = mat[4];
  y1 = mat[5];
  x2 = x1 + width;
  y2 = y1 + height;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);
  setCairo (cr);
  cairo_translate (cr, 0, height);
  cairo_scale (cr, width, -height);

  CairoOutputDev::drawMaskedImage(state, ref, str, width, height, colorMap,
				  maskStr, maskWidth, maskHeight, maskInvert);

  image = new CairoImage (surface, x1, y1, x2, y2);
  saveImage (image);
  
  setCairo (NULL);
  cairo_surface_destroy (surface);
  cairo_destroy (cr);
}

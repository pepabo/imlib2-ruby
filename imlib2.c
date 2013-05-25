/************************************************************************/
/* Copyright (C) 2002-2004 Paul Duncan, and various contributors.       */
/*                                                                      */
/* Permission is hereby granted, free of charge, to any person          */
/* obtaining a copy of this software and associated documentation files */
/* (the "Software"), to deal in the Software without restriction,       */
/* including without limitation the rights to use, copy, modify, merge, */
/* publish, distribute, sublicense, and/or sell copies of the Software, */
/* and to permit persons to whom the Software is furnished to do so,    */
/* subject to the following conditions:                                 */
/*                                                                      */
/* The above copyright notice and this permission notice shall be       */
/* included in all copies of the Software, its documentation and        */
/* marketing & publicity materials, and acknowledgment shall be given   */
/* in the documentation, materials and software packages that this      */
/* Software was used.                                                   */
/*                                                                      */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,      */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF   */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                */
/* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY     */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE    */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.               */
/************************************************************************/

#include <stdio.h>

/* Note: X support is disabled in the Makefile; it currently does not
 * compile */
#ifndef X_DISPLAY_MISSING
#include <X11/Xlib.h>
#endif /* !X_DISPLAY_MISSING */

#include <Imlib2.h>
#include <ruby.h>

#define UNUSED(a) ((void) (a))
#define VERSION "0.5.2"

/****************************/
/* CLASS AND MODULE GLOBALS */
/****************************/
static VALUE mImlib2,
             mError,
             cFileError,
             cDeletedError,
             cBorder,
             mCache,
             mColor,
             mTextDir,
             cRgbaColor,
             cHsvaColor,
             cHlsaColor,
             cCmyaColor,
             cContext,
             cGradient,
             cImage,
             cFilter,
             cFont,
             cColorMod,
             cPolygon,
             mOp,
             mOperation,
             mEncoding; 

#ifndef X_DISPLAY_MISSING
static VALUE mX11,
             cDisplay,
             cPixmap,
             cVisual,
             cDrawable,
             cColormap;
#endif /* X_DISPLAY_MISSING */


/***************/
/* WORKAROUNDS */
/***************/
#ifdef DISABLE_DRAW_PIXEL_WORKAROUND
static char draw_pixel_workaround = 0;
#else
static char draw_pixel_workaround = 1;
#endif /* DISABLE_DRAW_PIXEL_WORKAROUND */


/**********************************/
/* Imlib2::FileError EXCEPTIONS   */
/* (exceptions and error strings) */
/**********************************/
static struct { 
    VALUE exception;
    char *name, 
         *description;
} imlib_errors[] = {
  { 0, "NONE",                         "No error" },
  { 0, "FILE_DOES_NOT_EXIST",          "File does not exist" },
  { 0, "FILE_IS_DIRECTORY",            "File is directory" },
  { 0, "PERMISSION_DENIED_TO_READ",    "Permission denied to read" },
  { 0, "NO_LOADER_FOR_FILE_FORMAT",    "No loader for file format" },
  { 0, "PATH_TOO_LONG",                "Path too long" },
  { 0, "PATH_COMPONENT_NON_EXISTANT",  "Path component nonexistant" },
  { 0, "PATH_COMPONENT_NOT_DIRECTORY", "Path component not directory" },
  { 0, "PATH_POINTS_OUTSIDE_ADDRESS_SPACE","Path points outside address space"},
  { 0, "TOO_MANY_SYMBOLIC_LINKS",      "Too many symbolic links" },
  { 0, "OUT_OF_MEMORY",                "Out of memory" },
  { 0, "OUT_OF_FILE_DESCRIPTORS",      "Out of file descriptors" },
  { 0, "PERMISSION_DENIED_TO_WRITE",   "Permission denied to write" },
  { 0, "OUT_OF_DISK_SPACE",            "Out of disk space" },
  { 0, "UNKNOWN",                      "Unknown or unspecified error" }
};

typedef struct {
  Imlib_Image im;
} ImStruct;

#define GET_AND_CHECK_IMAGE(src, image) do { \
  Data_Get_Struct((src), ImStruct, (image)); \
  if (!(image)->im) { \
    rb_raise(cDeletedError, "image deleted"); \
    return Qnil; \
  } \
} while (0)

/********************************/
/* COLOR CLASSES                */
/* (RGBA is Imlib_Color struct) */
/********************************/
typedef struct {
  double hue,
         saturation,
         value;
  int    alpha;
} HsvaColor;

typedef struct {
  double hue,
         lightness,
         saturation;
  int    alpha;
} HlsaColor;

typedef struct {
  int cyan,
      magenta, 
      yellow,
      alpha;
} CmyaColor;


/*********************/
/* UTILITY FUNCTIONS */
/*********************/
/* raise an Imlib2::FileError exception based on an Imlib2 error type */
static void raise_imlib_error(const char *path, int err) {
  char buf[1024];

  /* sanity check on error message */
  if (err < IMLIB_LOAD_ERROR_NONE || err > IMLIB_LOAD_ERROR_UNKNOWN)
    err = IMLIB_LOAD_ERROR_UNKNOWN;

  /* add filename and path to buffer */
  snprintf(buf, sizeof(buf), "\"%s\": %s", path, imlib_errors[err].description);
  
  /* raise exception */
  rb_raise(imlib_errors[err].exception, buf);
}

/* set the context color -- polymorphic based on color class */
static void set_context_color(VALUE color) {
  if (rb_obj_is_kind_of(color, cRgbaColor) == Qtrue) {
    Imlib_Color *c;
    Data_Get_Struct(color, Imlib_Color, c);
    imlib_context_set_color(c->red, c->green, c->blue, c->alpha);
  } else if (rb_obj_is_kind_of(color, cHsvaColor) == Qtrue) {
    HsvaColor *c;
    Data_Get_Struct(color, HsvaColor, c);
    imlib_context_set_color_hsva(c->hue, c->saturation, c->value, c->alpha);
  } else if (rb_obj_is_kind_of(color, cHlsaColor) == Qtrue) {
    HlsaColor *c;
    Data_Get_Struct(color, HlsaColor, c);
    imlib_context_set_color_hsva(c->hue, c->lightness, c->saturation, c->alpha);
  } else if (rb_obj_is_kind_of(color, cCmyaColor) == Qtrue) {
    CmyaColor *c;
    Data_Get_Struct(color, CmyaColor, c);
    imlib_context_set_color_hsva(c->cyan, c->magenta, c->yellow, c->alpha);
  } else {
    rb_raise(rb_eTypeError, "Invalid argument type (not "
                            "Imlib2::Color::RgbaColor, "
                            "Imlib2::Color::HvsaColor, "
                            "Imlib2::Color::HslaColor, or "
                            "Imlib2::Color::CmyaColor)");
  }
}

/* don't actually free this value, but make ruby think it's gone */
/* static void dont_free(void *val) { UNUSED(val) } */

/******************/
/* BORDER METHODS */
/******************/
/*
 * Returns a new Imlib2::Border object.
 *
 * Examples:
 *   left, top, right, bottom = 10, 10, 20, 20
 *   border = Imlib2::Border.new left, top, right, bottom
 *
 *   values = [10, 10, 20, 20]
 *   border = Imlib2::Border.new values
 *
 *   edges = {
 *     'left'   => 10,
 *     'right'  => 20,
 *     'top'    => 10,
 *     'bottom' => 20,
 *   }
 *   border = Imlib2::Border.new edges
 */
VALUE border_new(int argc, VALUE *argv, VALUE klass) {
  Imlib_Border *border;
  VALUE b_o;
  
  border = malloc(sizeof(Imlib_Border));
  memset(border, 0, sizeof(Imlib_Border));

  b_o = Data_Wrap_Struct(klass, 0, free, border);
  rb_obj_call_init(b_o, argc, argv);

  return b_o;
}
  
/*
 * Imlib2::Border constructor.
 *
 * Parameters are identical to Imlib2::Border::new.
 */
static VALUE border_init(int argc, VALUE *argv, VALUE self) {
  Imlib_Border *border;
  
  Data_Get_Struct(self, Imlib_Border, border);

  switch (argc) {
    case 1:
      /* must be either an array or a hash */
      switch (TYPE(argv[0])) {
        case T_HASH:
          border->left = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("left")));
          border->top = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("top")));
          border->right = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("right")));
          border->bottom = NUM2INT(rb_hash_aref(argv[0],rb_str_new2("bottom")));
          break;
        case T_ARRAY:
          border->left = NUM2INT(rb_ary_entry(argv[0], 0));
          border->top = NUM2INT(rb_ary_entry(argv[0], 1));
          border->right = NUM2INT(rb_ary_entry(argv[0], 2));
          border->bottom = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 4:
      /* it's a list of values */
      border->left = NUM2INT(argv[0]);
      border->top = NUM2INT(argv[1]);
      border->right = NUM2INT(argv[2]);
      border->bottom = NUM2INT(argv[3]);
      break;
    default:
      /* no initializing values */
      break;
  }

  return self;
}

/*
 * Get the left width (in pixels) of a border.
 *
 * Examples:
 *   edge = border.left
 *   edge = border.l
 */
static VALUE border_left(VALUE self) {
  Imlib_Border *b;
  Data_Get_Struct(self, Imlib_Border, b);
  return INT2FIX(b->left);
}

/*
 * Set the left width (in pixels) of a border.
 *
 * Examples:
 *   border.left = 10
 *   border.l = 10
 */
static VALUE border_set_left(VALUE self, VALUE val) {
  Imlib_Border *b;
  Data_Get_Struct(self, Imlib_Border, b);
  b->left = NUM2INT(val);
  return val;
}

/*
 * Get the right width (in pixels) of a border.
 *
 * Examples:
 *   edge = border.right
 *   edge = border.r
 */
static VALUE border_right(VALUE self) {
  Imlib_Border *b;
  Data_Get_Struct(self, Imlib_Border, b);
  return INT2FIX(b->right);
}

/*
 * Set the right width (in pixels) of a border.
 *
 * Examples:
 *   border.right = 10
 *   border.r = 10
 */
static VALUE border_set_right(VALUE self, VALUE val) {
  Imlib_Border *b;
  Data_Get_Struct(self, Imlib_Border, b);
  b->right = NUM2INT(val);
  return val;
}

/*
 * Get the top height (in pixels) of a border.
 *
 * Examples:
 *   edge = border.top
 *   edge = border.t
 */
static VALUE border_top(VALUE self) {
  Imlib_Border *b;
  Data_Get_Struct(self, Imlib_Border, b);
  return INT2FIX(b->top);
}

/*
 * Set the top height (in pixels) of a border.
 *
 * Examples:
 *   border.top = 10
 *   border.t = 10
 */
static VALUE border_set_top(VALUE self, VALUE val) {
  Imlib_Border *b;
  Data_Get_Struct(self, Imlib_Border, b);
  b->top = NUM2INT(val);
  return val;
}

/*
 * Get the bottom height (in pixels) of a border.
 *
 * Examples:
 *   edge = border.bottom
 *   edge = border.b
 */
static VALUE border_bottom(VALUE self) {
  Imlib_Border *b;
  Data_Get_Struct(self, Imlib_Border, b);
  return INT2FIX(b->bottom);
}

/*
 * Set the bottom height (in pixels) of a border.
 *
 * Examples:
 *   border.bottom = 10
 *   border.b = 10
 */
static VALUE border_set_bottom(VALUE self, VALUE val) {
  Imlib_Border *b;
  Data_Get_Struct(self, Imlib_Border, b);
  b->bottom = NUM2INT(val);
  return val;
}

/*****************/
/* CACHE METHODS */
/*****************/
/*
 * Return the size (in bytes) of the application-wide image cache.
 *
 * Examples:
 *   size = Imlib2::Cache::get_image_cache
 *   size = Imlib2::Cache::image_cache
 *   size = Imlib2::Cache::image
 */
static VALUE cache_image(VALUE klass) {
  UNUSED(klass);
  return INT2FIX(imlib_get_cache_size());
}

/*
 * Set the size (in bytes) of the application-wide image cache.
 *
 * Examples:
 *   new_size = 16 * 1024 ** 2               # 16 megabytes
 *   Imlib2::Cache::set_image_cache new_size
 *
 *   new_size = 16 * 1024 ** 2               # 16 megabytes
 *   Imlib2::Cache::image_cache = new_size
 *
 *   new_size = 16 * 1024 ** 2               # 16 megabytes
 *   Imlib2::Cache::image = new_size
 */
static VALUE cache_set_image(VALUE klass, VALUE val) {
  UNUSED(klass);
  imlib_set_cache_size(NUM2INT(val));
  return Qtrue;
}

/*
 * Return the size (in bytes) of the application-wide font cache.
 *
 * Examples:
 *   size = Imlib2::Cache::get_font_cache
 *   size = Imlib2::Cache::font_cache
 *   size = Imlib2::Cache::font
 */
static VALUE cache_font(VALUE klass) {
  UNUSED(klass);
  return INT2FIX(imlib_get_font_cache_size());
}

/*
 * Set the size (in bytes) of the application-wide font cache.
 * Examples:
 *   new_size = 8 * 1024 ** 2                # 8 megabytes
 *   Imlib2::Cache::set_font_cache new_size
 *
 *   new_size = 8 * 1024 ** 2                # 8 megabytes
 *   Imlib2::Cache::font_cache = new_size
 *
 *   new_size = 8 * 1024 ** 2                # 8 megabytes
 *   Imlib2::Cache::font = new_size
 */
static VALUE cache_set_font(VALUE klass, VALUE val) {
  UNUSED(klass);
  imlib_set_font_cache_size(NUM2INT(val));
  return Qtrue;
}

/*
 * Flush and return the new size (in bytes) of the application-wide font
 * cache.
 *
 * Example:
 *   new_size = Imlib2::Cache::flush_font_cache
 */
static VALUE cache_flush_font(VALUE klass) {
  imlib_flush_font_cache();
  return cache_font(klass);
}

/**********************/
/* RGBA COLOR METHODS */
/**********************/
/*
 * Returns a new Imlib2::Color::RgbaColor.
 *
 * Examples:
 *   r, g, b, a = 255, 0, 0, 255
 *   border = Imlib2::Color::RgbaColor.new r, g, b, a
 *
 *   values = [255, 0, 0, 255]
 *   border = Imlib2::Color::RgbaColor.new values
 *
 */
VALUE rgba_color_new(int argc, VALUE *argv, VALUE klass) {
  Imlib_Color *color;
  VALUE c_o;
  
  color = malloc(sizeof(Imlib_Color));
  memset(color, 0, sizeof(Imlib_Color));

  c_o = Data_Wrap_Struct(klass, 0, free, color);
  rb_obj_call_init(c_o, argc, argv);

  return c_o;
}
  
/*
 * Imlib2::Color::RgbaColor constructor.
 *
 * Parameters are identical to Imlib2::Color::RgbaColor::new.
 */
static VALUE rgba_color_init(int argc, VALUE *argv, VALUE self) {
  Imlib_Color *color = NULL;
  
  Data_Get_Struct(self, Imlib_Color, color);
  switch (argc) {
    case 1:
      /* must be either an array or a hash */
      switch (TYPE(argv[0])) {
        case T_HASH:
          color->red = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("red")));
          color->green = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("green")));
          color->blue = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("blue")));
          color->alpha = NUM2INT(rb_hash_aref(argv[0],rb_str_new2("alpha")));
          break;
        case T_ARRAY:
          color->red = NUM2INT(rb_ary_entry(argv[0], 0));
          color->green = NUM2INT(rb_ary_entry(argv[0], 1));
          color->blue = NUM2INT(rb_ary_entry(argv[0], 2));
          color->alpha = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid arguments (not array or hash)");
      }
      break;
    case 4:
      /* it's a list of values */
      color->red = NUM2INT(argv[0]);
      color->green = NUM2INT(argv[1]);
      color->blue = NUM2INT(argv[2]);
      color->alpha = NUM2INT(argv[3]);
      break;
    default:
      /* no initializing values */
      break;
  }

  return self;
}

/*
 * Get the red element of a RgbaColor object.
 *
 * Examples:
 *   amount = color.red
 *   amount = color.r
 */
static VALUE rgba_color_red(VALUE self) {
  Imlib_Color *b;
  Data_Get_Struct(self, Imlib_Color, b);
  return INT2FIX(b->red);
}

/*
 * Set the red element of a RgbaColor object.
 *
 * Examples:
 *   color.red = 255
 *   color.r = 255
 */
static VALUE rgba_color_set_red(VALUE self, VALUE val) {
  Imlib_Color *b;
  Data_Get_Struct(self, Imlib_Color, b);
  b->red = NUM2INT(val);
  return val;
}

/*
 * Get the blue element of a RgbaColor object.
 *
 * Examples:
 *   amount = color.blue
 *   amount = color.b
 */
static VALUE rgba_color_blue(VALUE self) {
  Imlib_Color *b;
  Data_Get_Struct(self, Imlib_Color, b);
  return INT2FIX(b->blue);
}

/*
 * Set the blue element of a RgbaColor object.
 *
 * Examples:
 *   color.blue = 255
 *   color.b = 255
 */
static VALUE rgba_color_set_blue(VALUE self, VALUE val) {
  Imlib_Color *b;
  Data_Get_Struct(self, Imlib_Color, b);
  b->blue = NUM2INT(val);
  return val;
}

/*
 * Get the green element of a RgbaColor object.
 *
 * Examples:
 *   amount = color.green
 *   amount = color.g
 */
static VALUE rgba_color_green(VALUE self) {
  Imlib_Color *b;
  Data_Get_Struct(self, Imlib_Color, b);
  return INT2FIX(b->green);
}

/*
 * Set the green element of a RgbaColor object.
 *
 * Examples:
 *   color.green = 255
 *   color.g = 255
 */
static VALUE rgba_color_set_green(VALUE self, VALUE val) {
  Imlib_Color *b;
  Data_Get_Struct(self, Imlib_Color, b);
  b->green = NUM2INT(val);
  return val;
}

/*
 * Get the alpha element of a RgbaColor object.
 *
 * Examples:
 *   amount = color.alpha
 *   amount = color.a
 */
static VALUE rgba_color_alpha(VALUE self) {
  Imlib_Color *b;
  Data_Get_Struct(self, Imlib_Color, b);
  return INT2FIX(b->alpha);
}

/*
 * Set the alpha element of a RgbaColor object.
 *
 * Examples:
 *   color.alpha = 255
 *   color.a = 255
 */
static VALUE rgba_color_set_alpha(VALUE self, VALUE val) {
  Imlib_Color *b;
  Data_Get_Struct(self, Imlib_Color, b);
  b->alpha = NUM2INT(val);
  return val;
}


/**********************/
/* HSVA COLOR METHODS */
/**********************/
/*
 * Returns a new Imlib2::Color::HsvaColor.
 *
 * Examples:
 *   h, s, v, a = 255, 0, 0, 255
 *   border = Imlib2::Color::HsvaColor.new h, s, v, a
 *
 *   values = [255, 0, 0, 255]
 *   border = Imlib2::Color::HsvaColor.new values
 *
 */
VALUE hsva_color_new(int argc, VALUE *argv, VALUE klass) {
  HsvaColor *color;
  VALUE c_o;
  
  color = malloc(sizeof(HsvaColor));
  memset(color, 0, sizeof(HsvaColor));

  c_o = Data_Wrap_Struct(klass, 0, free, color);
  rb_obj_call_init(c_o, argc, argv);

  return c_o;
}
  
/*
 * Imlib2::Color::HsvaColor constructor.
 *
 * Parameters are identical to Imlib2::Color::HsvaColor::new.
 */
static VALUE hsva_color_init(int argc, VALUE *argv, VALUE self) {
  HsvaColor *color;
  
  Data_Get_Struct(self, HsvaColor, color);

  switch (argc) {
    case 1:
      /* must be either an array or a hash */
      switch (TYPE(argv[0])) {
        case T_HASH:
          color->hue = NUM2DBL(rb_hash_aref(argv[0], rb_str_new2("hue")));
          color->saturation = NUM2DBL(rb_hash_aref(argv[0], rb_str_new2("saturation")));
          color->value = NUM2DBL(rb_hash_aref(argv[0], rb_str_new2("value")));
          color->alpha = NUM2INT(rb_hash_aref(argv[0],rb_str_new2("alpha")));
          break;
        case T_ARRAY:
          color->hue = NUM2DBL(rb_ary_entry(argv[0], 0));
          color->saturation = NUM2DBL(rb_ary_entry(argv[0], 1));
          color->value = NUM2DBL(rb_ary_entry(argv[0], 2));
          color->alpha = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 4:
      /* it's a list of values */
      color->hue = NUM2DBL(argv[0]);
      color->saturation = NUM2DBL(argv[1]);
      color->value = NUM2DBL(argv[2]);
      color->alpha = NUM2INT(argv[3]);
      break;
    default:
      /* no initializing values */
      break;
  }

  return self;
}

/*
 * Get the hue element of a HsvaColor object.
 *
 * Examples:
 *   amount = color.hue
 *   amount = color.h
 */
static VALUE hsva_color_hue(VALUE self) {
  HsvaColor *b;
  Data_Get_Struct(self, HsvaColor, b);
  return rb_float_new(b->hue);
}

/*
 * Set the hue element of a HsvaColor object.
 *
 * Examples:
 *   color.hue = 255
 *   color.h = 255
 */
static VALUE hsva_color_set_hue(VALUE self, VALUE val) {
  HsvaColor *b;
  Data_Get_Struct(self, HsvaColor, b);
  b->hue = NUM2DBL(val);
  return val;
}

/*
 * Get the value element of a HsvaColor object.
 *
 * Examples:
 *   amount = color.value
 *   amount = color.v
 */
static VALUE hsva_color_value(VALUE self) {
  HsvaColor *b;
  Data_Get_Struct(self, HsvaColor, b);
  return rb_float_new(b->value);
}

/*
 * Set the value element of a HsvaColor object.
 *
 * Examples:
 *   color.value = 255
 *   color.v = 255
 */
static VALUE hsva_color_set_value(VALUE self, VALUE val) {
  HsvaColor *b;
  Data_Get_Struct(self, HsvaColor, b);
  b->value = NUM2DBL(val);
  return val;
}

/*
 * Get the saturation element of a HsvaColor object.
 *
 * Examples:
 *   amount = color.saturation
 *   amount = color.s
 */
static VALUE hsva_color_saturation(VALUE self) {
  HsvaColor *b;
  Data_Get_Struct(self, HsvaColor, b);
  return rb_float_new(b->saturation);
}

/*
 * Set the saturation element of a HsvaColor object.
 *
 * Examples:
 *   color.saturation = 255
 *   color.s = 255
 */
static VALUE hsva_color_set_saturation(VALUE self, VALUE val) {
  HsvaColor *b;
  Data_Get_Struct(self, HsvaColor, b);
  b->saturation = NUM2DBL(val);
  return val;
}

/*
 * Get the alpha element of a HsvaColor object.
 *
 * Examples:
 *   amount = color.alpha
 *   amount = color.a
 */
static VALUE hsva_color_alpha(VALUE self) {
  HsvaColor *b;
  Data_Get_Struct(self, HsvaColor, b);
  return INT2FIX(b->alpha);
}

/*
 * Set the alpha element of a HsvaColor object.
 *
 * Examples:
 *   color.alpha = 255
 *   color.a = 255
 */
static VALUE hsva_color_set_alpha(VALUE self, VALUE val) {
  HsvaColor *b;
  Data_Get_Struct(self, HsvaColor, b);
  b->alpha = NUM2INT(val);
  return val;
}


/**********************/
/* HLSA COLOR METHODS */
/**********************/
/*
 * Returns a new Imlib2::Color::HlsaColor.
 *
 * Examples:
 *   h, l, s, a = 255, 0, 0, 255
 *   border = Imlib2::Color::HlsaColor.new h, l, s, a
 *
 *   values = [255, 0, 0, 255]
 *   border = Imlib2::Color::HlsaColor.new values
 *
 */
VALUE hlsa_color_new(int argc, VALUE *argv, VALUE klass) {
  HlsaColor *color;
  VALUE c_o;
  
  color = malloc(sizeof(HlsaColor));
  memset(color, 0, sizeof(HlsaColor));

  c_o = Data_Wrap_Struct(klass, 0, free, color);
  rb_obj_call_init(c_o, argc, argv);

  return c_o;
}
  
/*
 * Imlib2::Color::HlsaColor constructor.
 *
 * Parameters are identical to Imlib2::Color::HlsaColor::new.
 */
static VALUE hlsa_color_init(int argc, VALUE *argv, VALUE self) {
  HlsaColor *color;
  
  Data_Get_Struct(self, HlsaColor, color);

  switch (argc) {
    case 1:
      /* must be either an array or a hash */
      switch (TYPE(argv[0])) {
        case T_HASH:
          color->hue = NUM2DBL(rb_hash_aref(argv[0], rb_str_new2("hue")));
          color->lightness = NUM2DBL(rb_hash_aref(argv[0], rb_str_new2("lightness")));
          color->saturation = NUM2DBL(rb_hash_aref(argv[0], rb_str_new2("saturation")));
          color->alpha = NUM2INT(rb_hash_aref(argv[0],rb_str_new2("alpha")));
          break;
        case T_ARRAY:
          color->hue = NUM2DBL(rb_ary_entry(argv[0], 0));
          color->lightness = NUM2DBL(rb_ary_entry(argv[0], 1));
          color->saturation = NUM2DBL(rb_ary_entry(argv[0], 2));
          color->alpha = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 4:
      /* it's a list of values */
      color->hue = NUM2DBL(argv[0]);
      color->lightness = NUM2DBL(argv[1]);
      color->saturation = NUM2DBL(argv[2]);
      color->alpha = NUM2INT(argv[3]);
      break;
    default:
      /* no initializing values */
      break;
  }

  return self;
}

/*
 * Get the hue element of a HlsaColor object.
 *
 * Examples:
 *   amount = color.hue
 *   amount = color.h
 */
static VALUE hlsa_color_hue(VALUE self) {
  HlsaColor *b;
  Data_Get_Struct(self, HlsaColor, b);
  return rb_float_new(b->hue);
}

/*
 * Set the hue element of a HlsaColor object.
 *
 * Examples:
 *   color.hue = 255
 *   color.h = 255
 */
static VALUE hlsa_color_set_hue(VALUE self, VALUE val) {
  HlsaColor *b;
  Data_Get_Struct(self, HlsaColor, b);
  b->hue = NUM2DBL(val);
  return val;
}

/*
 * Get the saturation element of a HlsaColor object.
 *
 * Examples:
 *   amount = color.saturation
 *   amount = color.s
 */
static VALUE hlsa_color_saturation(VALUE self) {
  HlsaColor *b;
  Data_Get_Struct(self, HlsaColor, b);
  return rb_float_new(b->saturation);
}

/*
 * Set the saturation element of a HlsaColor object.
 *
 * Examples:
 *   color.saturation = 255
 *   color.h = 255
 */
static VALUE hlsa_color_set_saturation(VALUE self, VALUE val) {
  HlsaColor *b;
  Data_Get_Struct(self, HlsaColor, b);
  b->saturation = NUM2DBL(val);
  return val;
}

/*
 * Get the lightness element of a HlsaColor object.
 *
 * Examples:
 *   amount = color.lightness
 *   amount = color.l
 */
static VALUE hlsa_color_lightness(VALUE self) {
  HlsaColor *b;
  Data_Get_Struct(self, HlsaColor, b);
  return rb_float_new(b->lightness);
}

/*
 * Set the lightness element of a HlsaColor object.
 *
 * Examples:
 *   color.lightness = 255
 *   color.l = 255
 */
static VALUE hlsa_color_set_lightness(VALUE self, VALUE val) {
  HlsaColor *b;
  Data_Get_Struct(self, HlsaColor, b);
  b->lightness = NUM2DBL(val);
  return val;
}

/*
 * Get the alpha element of a HlsaColor object.
 *
 * Examples:
 *   amount = color.alpha
 *   amount = color.a
 */
static VALUE hlsa_color_alpha(VALUE self) {
  HlsaColor *b;
  Data_Get_Struct(self, HlsaColor, b);
  return INT2FIX(b->alpha);
}

/*
 * Set the alpha element of a HlsaColor object.
 *
 * Examples:
 *   color.alpha = 255
 *   color.a = 255
 */
static VALUE hlsa_color_set_alpha(VALUE self, VALUE val) {
  HlsaColor *b;
  Data_Get_Struct(self, HlsaColor, b);
  b->alpha = NUM2INT(val);
  return val;
}


/**********************/
/* CMYA COLOR METHODS */
/**********************/
/*
 * Returns a new Imlib2::Color::CmyaColor.
 *
 * Examples:
 *   c, m, y, a = 255, 0, 0, 255
 *   border = Imlib2::Color::CmyaColor.new c, m, y, a
 *
 *   values = [255, 0, 0, 255]
 *   border = Imlib2::Color::CmyaColor.new values
 *
 */
VALUE cmya_color_new(int argc, VALUE *argv, VALUE klass) {
  CmyaColor *color;
  VALUE c_o;
  
  color = malloc(sizeof(CmyaColor));
  memset(color, 0, sizeof(CmyaColor));

  c_o = Data_Wrap_Struct(klass, 0, free, color);
  rb_obj_call_init(c_o, argc, argv);

  return c_o;
}
  
/*
 * Imlib2::Color::CmyaColor constructor.
 *
 * Parameters are identical to Imlib2::Color::CmyaColor::new.
 */
static VALUE cmya_color_init(int argc, VALUE *argv, VALUE self) {
  CmyaColor *color;
  
  Data_Get_Struct(self, CmyaColor, color);

  switch (argc) {
    case 1:
      /* must be either an array or a hash */
      switch (TYPE(argv[0])) {
        case T_HASH:
          color->cyan = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("cyan")));
          color->magenta = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("magenta")));
          color->yellow = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("yellow")));
          color->alpha = NUM2INT(rb_hash_aref(argv[0],rb_str_new2("alpha")));
          break;
        case T_ARRAY:
          color->cyan = NUM2INT(rb_ary_entry(argv[0], 0));
          color->magenta = NUM2INT(rb_ary_entry(argv[0], 1));
          color->yellow = NUM2INT(rb_ary_entry(argv[0], 2));
          color->alpha = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 4:
      /* it's a list of values */
      color->cyan = NUM2INT(argv[0]);
      color->magenta = NUM2INT(argv[1]);
      color->yellow = NUM2INT(argv[2]);
      color->alpha = NUM2INT(argv[3]);
      break;
    default:
      /* no initializing values */
      break;
  }

  return self;
}

/*
 * Get the cyan element of a CmyaColor object.
 *
 * Examples:
 *   amount = color.cyan
 *   amount = color.c
 */
static VALUE cmya_color_cyan(VALUE self) {
  CmyaColor *b;
  Data_Get_Struct(self, CmyaColor, b);
  return INT2FIX(b->cyan);
}

/*
 * Set the cyan element of a CmyaColor object.
 *
 * Examples:
 *   color.cyan = 255
 *   color.c = 255
 */
static VALUE cmya_color_set_cyan(VALUE self, VALUE val) {
  CmyaColor *b;
  Data_Get_Struct(self, CmyaColor, b);
  b->cyan = NUM2INT(val);
  return val;
}

/*
 * Get the yellow element of a CmyaColor object.
 *
 * Examples:
 *   amount = color.yellow
 *   amount = color.y
 */
static VALUE cmya_color_yellow(VALUE self) {
  CmyaColor *b;
  Data_Get_Struct(self, CmyaColor, b);
  return INT2FIX(b->yellow);
}

/*
 * Set the yellow element of a CmyaColor object.
 *
 * Examples:
 *   color.yellow = 255
 *   color.y = 255
 */
static VALUE cmya_color_set_yellow(VALUE self, VALUE val) {
  CmyaColor *b;
  Data_Get_Struct(self, CmyaColor, b);
  b->yellow = NUM2INT(val);
  return val;
}

/*
 * Get the magenta element of a CmyaColor object.
 *
 * Examples:
 *   amount = color.magenta
 *   amount = color.m
 */
static VALUE cmya_color_magenta(VALUE self) {
  CmyaColor *b;
  Data_Get_Struct(self, CmyaColor, b);
  return INT2FIX(b->magenta);
}

/*
 * Set the magenta element of a CmyaColor object.
 *
 * Examples:
 *   color.magenta = 255
 *   color.m = 255
 */
static VALUE cmya_color_set_magenta(VALUE self, VALUE val) {
  CmyaColor *b;
  Data_Get_Struct(self, CmyaColor, b);
  b->magenta = NUM2INT(val);
  return val;
}

/*
 * Get the alpha element of a CmyaColor object.
 *
 * Examples:
 *   amount = color.alpha
 *   amount = color.a
 */
static VALUE cmya_color_alpha(VALUE self) {
  CmyaColor *b;
  Data_Get_Struct(self, CmyaColor, b);
  return INT2FIX(b->alpha);
}

/*
 * Set the alpha element of a CmyaColor object.
 *
 * Examples:
 *   color.alpha = 255
 *   color.a = 255
 */
static VALUE cmya_color_set_alpha(VALUE self, VALUE val) {
  CmyaColor *b;
  Data_Get_Struct(self, CmyaColor, b);
  b->alpha = NUM2INT(val);
  return val;
}

/***********************/
/* X11 DISPLAY METHODS */
/***********************/
#ifndef X_DISPLAY_MISSING
static void disp_free(void *disp) {
  XFree(disp);
}

/*
 * Open a connection to the specified X11 Display (or the default
 * display if nil is specified).  Raises a RunTimeError exception if a
 * connection to the specified display could not be opened.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   disp = Imlib2::X11::Display.new ':0.0'
 *
 */
VALUE display_new(VALUE klass, VALUE display) {
  Display *disp;
  VALUE self;

  if (display == Qnil) {
    char *env = getenv("DISPLAY");
    disp = XOpenDisplay(env);
  } else {
    disp = XOpenDisplay(StringValuePtr(display));
  }
  
  if (!disp)
    rb_raise(rb_eRuntimeError, "Couldn't open display");
  
  self = Data_Wrap_Struct(klass, 0, disp_free, disp);
  rb_obj_call_init(self, 0, NULL);

  return self;
}

/*
 * Constructor for Imlib2::X11::Display object.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * This method is currently just a placeholder.
 *
 */
static VALUE display_init(VALUE self) {
  return self;
}

/**********************/
/* X11 VISUAL METHODS */
/**********************/
static void visual_free(void *vis) {
  /* FIXME: we might be leaking visuals here, but I don't think we
   * should be freeing the default one */
  /* XFree(vis); */
}

/*
 * Get the default X11 Visual on the specified Display and screen.
 * Raises a RunTimeError exception if the visual could not be created.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   vis = Imlib2::X11::Visual.new display, screen_num
 *
 */
VALUE visual_new(VALUE klass, VALUE display, VALUE screen) {
  Display *disp;
  Visual *vis;
  VALUE self;

  if (display == Qnil)
    display = display_new(cDisplay, Qnil);
  Data_Get_Struct(display, Display, disp);

  vis = DefaultVisual(disp, NUM2INT(screen));
  
  self = Data_Wrap_Struct(klass, 0, visual_free, vis);
  rb_obj_call_init(self, 0, NULL);

  return self;
}

/*
 * Constructor for Imlib2::X11::Visual object.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * This method is currently just a placeholder.
 *
 */
static VALUE visual_init(VALUE self) {
  return self;
}

/************************/
/* X11 COLORMAP METHODS */
/************************/
static void cmap_free(void *vis) {
  /* FIXME: need to wrap colormap in struct to preserve display */
  /* XFree(vis); */
}

/*
 * Get the default X11 Colormap on the specified Display and screen.
 * Raises a RunTimeError exception if the colormap could not be
 * allocated.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   cmap = Imlib2::X11::Colormap.new display, screen_num
 *
 */
VALUE cmap_new(VALUE klass, VALUE display, VALUE screen) {
  Display *disp;
  Colormap *cmap;
  VALUE self;

  if (display == Qnil)
    display = display_new(cDisplay, Qnil);
  Data_Get_Struct(display, Display, disp);

  cmap = malloc(sizeof(Colormap));
  *cmap = DefaultColormap(disp, NUM2INT(screen));
  
  self = Data_Wrap_Struct(klass, 0, dont_free, cmap);
  rb_obj_call_init(self, 0, NULL);

  return self;
}

/*
 * Constructor for Imlib2::X11::Colormap object.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * This method is currently just a placeholder.
 *
 */
static VALUE cmap_init(VALUE self) {
  return self;
}

/************************/
/* X11 DRAWABLE METHODS */
/************************/
/*
 * Constructor for Imlib2::X11::Drawable object.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * This method is currently just a placeholder.
 *
 */
static VALUE drawable_init(VALUE self) {
  return self;
}
#endif /* X_DISPLAY_MISSING */

/*****************/
/* IMAGE METHODS */
/*****************/
static void im_struct_free(void *val) {
  ImStruct *im = (ImStruct*) val;
  
  if (im) {
    if (im->im) {
      imlib_context_set_image(im->im);
      imlib_free_image();
    }
    free(im);
  }
}

/*
 * Returns a new Imlib2::Image with the specified width and height.
 *
 * Examples:
 *   width, height = 640, 480
 *   image = Imlib2::Image.new width, height
 *
 *   width, height = 320, 240
 *   image = Imlib2::Image.create width, height
 */
VALUE image_new(VALUE klass, VALUE w, VALUE h) {
  ImStruct *im = malloc(sizeof(ImStruct));
  VALUE im_o;

  im->im = imlib_create_image(NUM2INT(w), NUM2INT(h));
  im_o = Data_Wrap_Struct(klass, 0, im_struct_free, im);
  rb_obj_call_init(im_o, 0, NULL);

  return im_o;
}

/*********************************/
/* DRAW PIXEL WORKAROUND METHODS */
/*********************************/
/*
 * Are we using the buggy imlib_image_draw_pixel() work-around?
 *
 * Versions of Imlib2 up to and including 1.0.5 had a broken
 * imlib_image_draw_pixel() call. Imlib2-Ruby has a work-around, which
 * simulates drawing a pixel with a 1x1 rectangle.  This method allows 
 * you to check whether the work-around behavior is enabled.
 *
 * Examples:
 *   puts 'work-around is enabled' if Imlib2::Image::draw_pixel_workaround?
 *   puts 'work-around is enabled' if Imlib2::Image::bypass_draw_pixel?
 *
 */
VALUE image_dp_workaround(VALUE klass) {
  UNUSED(klass);
  return draw_pixel_workaround ? Qtrue : Qfalse;
}

/*
 * Enable or disable imlib_image_draw_pixel() work-around.
 *
 * Versions of Imlib2 up to and including 1.0.5 had a broken
 * imlib_image_draw_pixel() call. Imlib2-Ruby has a work-around, which
 * simulates drawing a pixel with a 1x1 rectangle.  This method allows 
 * you to enable or disable the work-around behavior.
 *
 * Examples:
 *   Imlib2::Image::draw_pixel_workaround = false
 *   Imlib2::Image::bypass_draw_pixel = false
 *
 */
VALUE image_set_dp_workaround(VALUE klass, VALUE val) {
  UNUSED(klass);

  draw_pixel_workaround = (val == Qtrue);

  return val;
}

/*
 * Returns a new Imlib2::Image initialized with the specified data.
 *
 * Examples:
 *   other_image = Imlib2::Image.load 'sample_file.png'
 *   width, height = other_image.width, other_image.height
 *   data = other_image.data_for_reading_only
 *   image = Imlib2::Image.create_using_data width, height, data
 *
 */
VALUE image_create_using_data(VALUE klass, VALUE w, VALUE h, VALUE data) {
  ImStruct *im;
  VALUE im_o;

  im = malloc(sizeof(ImStruct));
  im->im = imlib_create_image_using_data(NUM2INT(w), NUM2INT(h), (DATA32 *) StringValuePtr (data));
  im_o = Data_Wrap_Struct(klass, 0, im_struct_free, im);
  rb_obj_call_init(im_o, 0, NULL);

  return im_o;
}

/*
 * Returns a new Imlib2::Image initialized with the specified data.
 *
 * Examples:
 *   other_image = Imlib2::Image.load 'sample_file.png'
 *   width, height = other_image.width, other_image.height
 *   data = other_image.data
 *   image = Imlib2::Image.create_using_copied_data width, height, data
 *
 */
VALUE image_create_using_copied_data(VALUE klass, VALUE w, VALUE h, VALUE data) {
  ImStruct *im;
  VALUE im_o;

  im = malloc(sizeof(ImStruct));
  im->im = imlib_create_image_using_copied_data(NUM2INT(w), NUM2INT(h), (DATA32 *) StringValuePtr (data));
  im_o = Data_Wrap_Struct(klass, 0, im_struct_free, im);
  rb_obj_call_init(im_o, 0, NULL);

  return im_o;
}

/*
 * Load an Imlib2::Image from a file (throws exceptions).
 *
 * Examples:
 *   image = Imlib2::Image.load 'sample_file.png'
 *
 *   begin 
 *     image = Imlib2::Image.load 'sample_file.png'
 *   rescue Imlib2::FileError
 *     $stderr.puts 'Couldn't load file: ' + $!
 *   end
 *
 */
static VALUE image_load(VALUE klass, VALUE filename) {
  ImStruct        *im;
  Imlib_Image      iim;
  Imlib_Load_Error err;
  VALUE            im_o = Qnil;
  char            *path;

  /* grab filename */
  path = StringValuePtr(filename);
  
  iim = imlib_load_image_with_error_return(path, &err);
  if (err == IMLIB_LOAD_ERROR_NONE) {
    im = malloc(sizeof(ImStruct));
    im->im = iim;
    im_o = Data_Wrap_Struct(klass, 0, im_struct_free, im);

    if (rb_block_given_p())
      rb_yield(im_o);
  } else {
    /* there was an error loading -- throw an exception if we weren't
     * passed a block */

    if (!rb_block_given_p())
      raise_imlib_error(path, err);
  }
  
  return im_o;
}

/*
 * Load an Imlib2::Image from a file (no exceptions or error).
 *
 * Imlib2::Image::load_image() provides access to the low-level
 * imlib_load_image() function.  You probably want to use 
 * Imlib2::Image::load() instead of Imlib2::Image::load_image().
 *
 * Examples:
 *   image = Imlib2::Image.load_image 'sample_file.png'
 *
 */
static VALUE image_load_image(VALUE klass, VALUE filename) {
  ImStruct *im = malloc(sizeof(ImStruct));
  VALUE im_o;

  im->im = imlib_load_image(StringValuePtr(filename));
  im_o = Data_Wrap_Struct(klass, 0, im_struct_free, im);
  
  return im_o;
}

/*
 * Load an Imlib2::Image from a file (no exceptions or error).
 *
 * Imlib2::Image::load_immediately() provides access to the low-level
 * imlib_load_image_immediately() function.  You probably want to use 
 * Imlib2::Image::load() instead of this function.
 *
 * Examples:
 *   image = Imlib2::Image.load_immediately 'sample_file.png'
 *
 */
static VALUE image_load_immediately(VALUE klass, VALUE filename) {
  ImStruct *im = malloc(sizeof(ImStruct));
  VALUE im_o;

  im->im = imlib_load_image_immediately(StringValuePtr(filename));
  im_o = Data_Wrap_Struct(klass, 0, im_struct_free, im);
  
  return im_o;
}

/*
 * Load an Imlib2::Image from a file (no caching, exception, or error).
 *
 * Imlib2::Image::load_without_cache() provides access to the low-level
 * imlib_load_image_without_cache() function.  You probably want to use 
 * Imlib2::Image::load() instead of this function.
 *
 * Examples:
 *   image = Imlib2::Image.load_without_cache 'sample_file.png'
 *
 */
static VALUE image_load_without_cache(VALUE klass, VALUE filename) {
  ImStruct *im = malloc(sizeof(ImStruct));
  VALUE im_o;

  im->im = imlib_load_image_without_cache(StringValuePtr(filename));
  im_o = Data_Wrap_Struct(klass, 0, im_struct_free, im);
  
  return im_o;
}

/*
 * Load an Imlib2::Image from a file (no caching, deferred loading,
 * exceptions, or errors).
 *
 * Imlib2::Image::load_immediately_without_cache() provides access to the
 * low-level imlib_load_image_immediately_without_cache() function.  You
 * probably want to use Imlib2::Image::load() instead of this function.
 *
 * Examples:
 *   image = Imlib2::Image.load_immediately_without_cache 'sample_file.png'
 *
 */
static VALUE image_load_immediately_without_cache(VALUE klass, VALUE filename) {
  ImStruct *im = malloc(sizeof(ImStruct));
  VALUE im_o;

  im->im = imlib_load_image_immediately_without_cache(StringValuePtr(filename));
  im_o = Data_Wrap_Struct(klass, 0, im_struct_free, im);
  
  return im_o;
}

/*
 * Load an Imlib2::Image from a file with a hash of the error value and
 * the image.
 *
 * Imlib2::Image::load_with_error_return() provides access to the
 * low-level imlib_load_image_with_error_return() function.  You
 * probably want to use Imlib2::Image::load() instead of this function.
 *
 * Examples:
 *   hash = Imlib2::Image.load_with_error_return 'sample_file.png'
 *   if hash['error'] == 0 # 0 is no error
 *     image = hash['image']
 *   end
 *
 */
static VALUE image_load_with_error_return(VALUE klass, VALUE filename) {
  ImStruct *im = malloc(sizeof(ImStruct));
  Imlib_Load_Error er;
  VALUE hash, im_o;
  
  im->im = imlib_load_image_with_error_return(StringValuePtr(filename), &er);
  im_o = Data_Wrap_Struct(klass, 0, im_struct_free, im);
  
  hash = rb_hash_new();
  rb_hash_aset(hash, rb_str_new2("image"), im_o);
  rb_hash_aset(hash, rb_str_new2("error"), INT2FIX(er));

  return hash;
}

/*
 * Save an Imlib2::Image to a file (throws an exception on error).
 * 
 * Examples:
 *   image.save 'output_file.png'
 *
 *   filename = 'output_file.jpg'
 *   begin
 *     image.save filename
 *   rescue Imlib2::FileError
 *     $stderr.puts "Couldn't save file \"#{filename}\": " + $!
 *   end
 *
 */
static VALUE image_save(VALUE self, VALUE val) {
  ImStruct *im;
  Imlib_Load_Error er;
  char *path;

  path = StringValuePtr(val);
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_save_image_with_error_return(path, &er);

  if (er == IMLIB_LOAD_ERROR_NONE)
    return self;
  if (er > IMLIB_LOAD_ERROR_UNKNOWN)
    er = IMLIB_LOAD_ERROR_UNKNOWN;
  raise_imlib_error(path, er);
  
  return Qnil;
}

/*
 * Save an Imlib2::Image to a file (no exception or error).
 * 
 * Provides access to the low-level imlib_save_image() call.  You
 * probably want to use Imlib2::Image::save() instead.
 *
 * Examples:
 *   image.save_image 'output_file.png'
 *
 */
static VALUE image_save_image(VALUE self, VALUE val) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_save_image(StringValuePtr(val));

  return self;
}

/*
 * Save an Imlib2::Image to a file (error returned as number).
 * 
 * Provides access to the low-level imlib_save_image_with_error_return()
 * call.  You probably want to use Imlib2::Image::save() instead.
 *
 * Examples:
 *   error = image.save_with_error_return 'output_file.png'
 *   puts 'an error occurred' unless error == 0
 *
 */
static VALUE image_save_with_error_return(VALUE self, VALUE val) {
  ImStruct *im;
  Imlib_Load_Error er;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_save_image_with_error_return(StringValuePtr(val), &er);

  if (er > IMLIB_LOAD_ERROR_UNKNOWN)
    er = IMLIB_LOAD_ERROR_UNKNOWN;
  
  return INT2FIX(er);
}

/*
 * Copy an Imlib2::Image
 *
 * Examples:
 *   new_image = old_image.clone
 *   new_image = old_image.dup
 *
 */
static VALUE image_clone(VALUE self) {
  ImStruct *old_im, *new_im;
  VALUE im_o;

  new_im = malloc(sizeof(ImStruct));
  GET_AND_CHECK_IMAGE(self, old_im);
  imlib_context_set_image(old_im->im);
  new_im->im = imlib_clone_image();
  im_o = Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);

  return im_o;
}

/*
 * Imlib2::Image constructor (currently just an empty placeholder).
 *
 */
static VALUE image_initialize(VALUE self) {
  return self;
}

/*
 * Free an Imlib2::Image object, and (optionally) de-cache it as well.
 *
 * Note: Any future operations on this image will raise an exception.
 *
 * Examples:
 *   # free image
 *   im.delete! 
 *
 *   # free image, and de-cache it too
 *   im.delete!(true)
 * 
 */
static VALUE image_delete(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;

  /* get image */
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  /* free image, and possibly de-cache it as well */
  if (argc > 0 && argv[0] != Qnil && argv[0] != Qfalse)
    imlib_free_image_and_decache();
  else
    imlib_free_image();

  /* set struct ptr to NULL */
  im->im = NULL;

  return Qnil;
}

/*
 * Return the width of an Imlib2::Image.
 *
 * Examples:
 *   w = image.width
 *   w = image.w
 *
 */
static VALUE image_width(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  return INT2FIX(imlib_image_get_width());
}

/*
 * Return the height of an Imlib2::Image.
 *
 * Examples:
 *   h = image.height
 *   h = image.h
 *
 */
static VALUE image_height(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  return INT2FIX(imlib_image_get_height());
}

/*
 * Return the filename of an Imlib2::Image.
 *
 * Examples:
 *   path = image.filename
 *
 */
static VALUE image_filename(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  return rb_str_new2(imlib_image_get_filename());
}

/*
 * Return a copy of an image's raw 32-bit data.
 *
 * Examples:
 *   raw = image.data
 *
 */
static VALUE image_data(VALUE self) {
  ImStruct *im;
  int       w, h;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  w = imlib_image_get_width();
  h = imlib_image_get_height();

  return rb_str_new((char*) imlib_image_get_data(), h * w * 4);
}

/*
 * Return a read-only reference to an image's raw 32-bit data.
 *
 * Examples:
 *   RAW_DATA = image.data_for_reading_only
 *   RAW_DATA = image.data!
 *
 */
static VALUE image_data_ro(VALUE self) {
  ImStruct *im;
  int       w, h;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  w = imlib_image_get_width();
  h = imlib_image_get_height();

  return rb_str_new((char*) imlib_image_get_data_for_reading_only(), h * w * 4);
}

/*
 * Fill an image using raw 32-bit data.
 *
 * Note: The new data buffer must be the same size (in bytes) as the
 * original data buffer.
 *
 * Examples:
 *   RAW_DATA = other_image.data!
 *   image.put_data RAW_DATA
 *
 *   RAW_DATA = other_image.data!
 *   image.data = RAW_DATA
 *
 */
static VALUE image_put_data(VALUE self, VALUE str) {
  ImStruct *im;
  DATA32 *old_data, *new_data;
  int w, h, old_size;

  /* get image, check it, then set the context */
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  /* get old data, calculate buffer size from width and height */
  old_data = imlib_image_get_data();
  w = imlib_image_get_width();
  h = imlib_image_get_height();
  old_size = w * h * 4;

  /* get new data, check size of buffer */
  new_data = (DATA32*) StringValuePtr(str);
  
  /* check size of new buffer */
  if (RSTRING(str)->len != old_size)
    rb_raise(rb_eArgError, "invalid buffer size");
  
  /* copy new data to old address */
  if (old_data != new_data)
    memcpy(old_data, new_data, old_size);
  
  /* actual put_back_data() call */
  imlib_image_put_back_data(old_data);

  /* return success */
  return Qtrue;
}

/*
 * Does this image have transparent or translucent regions?
 *
 * Examples:
 *   if image.has_alpha? 
 *     puts 'this image has alpha'
 *   end
 *
 */
static VALUE image_has_alpha(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  return imlib_image_has_alpha() ? Qtrue : Qfalse;
}

/*
 * Set image alpha transparency.
 *
 * Examples:
 *   image.set_has_alpha true 
 *   image.has_alpha = true 
 *
 */
static VALUE image_set_has_alpha(VALUE self, VALUE val) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_set_has_alpha(val == Qtrue);

  return val;
}

/*
 * Flag this image as changing on disk
 *
 * Examples:
 *   image.changes_on_disk
 *
 */
static VALUE image_changes_on_disk(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_set_changes_on_disk();

  return Qtrue;
}

/*
 * Get the Imlib2::Border of an Imlib2::Image
 *
 * Examples:
 *   border = image.get_border
 *   border = image.border
 *
 */
static VALUE image_get_border(VALUE self) {
  ImStruct *im;
  Imlib_Border *border;
  VALUE argv[4];
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  border = malloc(sizeof(Imlib_Border));
  imlib_image_get_border(border);
  argv[0] = INT2NUM(border->left);
  argv[1] = INT2NUM(border->top);
  argv[2] = INT2NUM(border->right);
  argv[3] = INT2NUM(border->bottom);
  free(border);

  return border_new(4, argv, cBorder);
}

/*
 * Set the Imlib2::Border of an Imlib2::Image
 *
 * Examples:
 *   image.set_border border
 *   image.border = border
 *
 */
static VALUE image_set_border(VALUE self, VALUE border) {
  ImStruct *im;
  Imlib_Border *b;
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  Data_Get_Struct(border, Imlib_Border, b);
  imlib_image_set_border(b);

  return border;
}

/*
 * Get the on-disk format of an Imlib2::Image
 *
 * Examples:
 *   format = image.get_format
 *   format = image.format
 *
 */
static VALUE image_get_format(VALUE self) {
  ImStruct *im;
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  return rb_str_new2(imlib_image_format());
}

/*
 * Set the on-disk format of an Imlib2::Image
 *
 * Examples:
 *   image.get_format 'png'
 *   image.format = 'png'
 *
 */
static VALUE image_set_format(VALUE self, VALUE format) {
  ImStruct *im;
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_set_format(StringValuePtr(format));

  return format;
}

/*
 * Set the irrelevant_format flag of an Imlib2::Image
 *
 * Examples:
 *   image.set_irrelevant_format true
 *   image.irrelevant_format = true
 *
 */
static VALUE image_irrelevant_format(VALUE self, VALUE val) {
  ImStruct *im;
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_set_irrelevant_format(val != Qfalse);

  return val;
}

/*
 * Set the irrelevant_border flag of an Imlib2::Image
 *
 * Examples:
 *   image.set_irrelevant_border true
 *   image.irrelevant_border = true
 *
 */
static VALUE image_irrelevant_border(VALUE self, VALUE val) {
  ImStruct *im;
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_set_irrelevant_border(val != Qfalse);

  return val;
}

/*
 * Set the irrelevant_alpha flag of an Imlib2::Image
 *
 * Examples:
 *   image.set_irrelevant_alpha true
 *   image.irrelevant_alpha = true
 *
 */
static VALUE image_irrelevant_alpha(VALUE self, VALUE val) {
  ImStruct *im;
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_set_irrelevant_alpha(val != Qfalse);

  return val;
}

/*
 * Get the Imlib2::Color::RgbaColor value of the pixel at x, y
 *
 * Examples:
 *   color = image.query_pixel 320, 240
 *   color = image.pixel 320, 240
 *
 */
static VALUE image_query_pixel(VALUE self, VALUE x, VALUE y) {
  ImStruct *im;
  Imlib_Color color;
  VALUE argv[4];
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  imlib_image_query_pixel(NUM2INT(x), NUM2INT(y), &color);
  argv[0] = INT2NUM(color.red);
  argv[1] = INT2NUM(color.green);
  argv[2] = INT2NUM(color.blue);
  argv[3] = INT2NUM(color.alpha);

  return rgba_color_new(4, argv, cRgbaColor);
}

/*
 * Get the Imlib2::Color::HsvaColor value of the pixel at x, y
 *
 * Examples:
 *   color = image.query_pixel_hsva 320, 240
 *   color = image.pixel_hsva 320, 240
 *
 */
static VALUE image_query_pixel_hsva(VALUE self, VALUE x, VALUE y) {
  ImStruct *im;
  float hue, saturation, value;
  int alpha;
  VALUE argv[4];
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  imlib_image_query_pixel_hsva(NUM2INT(x), NUM2INT(y), &hue, &saturation, &value, &alpha);
  argv[0] = rb_float_new(hue);
  argv[1] = rb_float_new(saturation);
  argv[2] = rb_float_new(value);
  argv[3] = INT2NUM(alpha);

  return hsva_color_new(4, argv, cHsvaColor);
}

/*
 * Get the Imlib2::Color::HlsaColor value of the pixel at x, y
 *
 * Examples:
 *   color = image.query_pixel_hlsa 320, 240
 *   color = image.pixel_hlsa 320, 240
 *
 */
static VALUE image_query_pixel_hlsa(VALUE self, VALUE x, VALUE y) {
  ImStruct *im;
  float hue, lightness, saturation;
  int alpha;
  VALUE argv[4];
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  imlib_image_query_pixel_hsva(NUM2INT(x), NUM2INT(y), &hue, &lightness, &saturation, &alpha);
  argv[0] = rb_float_new(hue);
  argv[1] = rb_float_new(lightness);
  argv[2] = rb_float_new(saturation);
  argv[3] = INT2NUM(alpha);

  return hlsa_color_new(4, argv, cHlsaColor);
}

/*
 * Get the Imlib2::Color::CmyaColor value of the pixel at x, y
 *
 * Examples:
 *   color = image.query_pixel_cmya 320, 240
 *   color = image.pixel_cmya 320, 240
 *
 */
static VALUE image_query_pixel_cmya(VALUE self, VALUE x, VALUE y) {
  ImStruct *im;
  CmyaColor color;
  VALUE argv[4];
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  imlib_image_query_pixel_cmya(NUM2INT(x), NUM2INT(y), &color.cyan, &color.magenta, &color.yellow, &color.alpha);
  argv[0] = INT2NUM(color.cyan);
  argv[1] = INT2NUM(color.magenta);
  argv[2] = INT2NUM(color.yellow);
  argv[3] = INT2NUM(color.alpha);

  return cmya_color_new(4, argv, cCmyaColor);
}

/*
 * Return a cropped copy of the image
 *
 * Examples:
 *   x, y, w, h = 10, 10, old_image.width - 10, old_image.height - 10
 *   new_image = old_image.crop x, y, w, h
 *
 *   rect = [10, 10, old_image.width - 10, old_image.height - 10]
 *   new_image = old_image.crop rect
 *
 *   x, y, w, h = 10, 10, old_image.width - 10, old_image.height - 10
 *   new_image = old_image.create_cropped x, y, w, h
 *
 *   rect = [10, 10, old_image.width - 10, old_image.height - 10]
 *   new_image = old_image.create_cropped rect
 *
 */
static VALUE image_crop(int argc, VALUE *argv, VALUE self) {
  ImStruct *old_im, *new_im;
  VALUE im_o;
  int x = 0, y = 0, w = 0, h = 0;
  
  switch (argc) {
    case 1:
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 4:
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      break;
    default:
      rb_raise(rb_eTypeError,"Invalid argument count (not 1 or 4)");
  }
  
  GET_AND_CHECK_IMAGE(self, old_im);
  imlib_context_set_image(old_im->im);
  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_create_cropped_image(x, y, w, h);
  im_o = Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);
  
  return im_o;
}

/*
 * Crop an image
 *
 * Examples:
 *   x, y, w, h = 10, 10, image.width - 10, image.height - 10
 *   image.crop! x, y, w, h
 *
 *   rect = [10, 10, image.width - 10, image.height - 10]
 *   image.crop! rect
 *
 *   x, y, w, h = 10, 10, image.width - 10, image.height - 10
 *   image.create_cropped! x, y, w, h
 *
 *   rect = [10, 10, image.width - 10, image.height - 10]
 *   image.create_cropped! rect
 *
 */
static VALUE image_crop_inline(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  Imlib_Image old_im;
  int x = 0, y = 0, w = 0, h = 0;
  
  switch (argc) {
    case 1:
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 4:
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      break;
    default:
      rb_raise(rb_eTypeError,"Invalid argument count (not 1 or 4)");
  }
  
  GET_AND_CHECK_IMAGE(self, im);
  old_im = im->im;
  imlib_context_set_image(old_im);
  im->im = imlib_create_cropped_image(x, y, w, h);
  imlib_context_set_image(old_im);
  imlib_free_image();

  return self;
}


/*
 * Create a cropped and scaled copy of an image
 *
 * Examples:
 *   iw, ih = old_image.width, old_image.height
 *   new_w, new_h = iw - 20, ih - 20
 *   x, y, w, h = 10, 10, iw - 10, ih - 10
 *   new_image = old_image.crop_scaled x, y, w, h, new_w, new_h
 *
 *   iw, ih = old_image.width, old_image.height
 *   new_w, new_h = iw - 20, ih - 20
 *   values = [10, 10, iw - 10, iw - 10, new_w, new_h]
 *   new_image = old_image.crop_scaled values
 *
 *   iw, ih = old_image.width, old_image.height
 *   new_w, new_h = iw - 20, ih - 20
 *   x, y, w, h = 10, 10, iw - 10, ih - 10
 *   new_image = old_image.create_crop_scaled x, y, w, h, new_w, new_h
 *
 *   iw, ih = old_image.width, old_image.height
 *   new_w, new_h = iw - 20, ih - 20
 *   values = [10, 10, iw - 10, iw - 10, new_w, new_h]
 *   new_image = old_image.create_crop_scaled values
 *
 */
static VALUE image_crop_scaled(int argc, VALUE *argv, VALUE self) {
  ImStruct *old_im, *new_im;
  VALUE im_o;
  int x = 0, y = 0, w = 0, h = 0, dw = 0, dh = 0;
  
  switch (argc) {
    case 1:
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          dw = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("dw")));
          dh = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("dh")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          dw = NUM2INT(rb_ary_entry(argv[0], 4));
          dh = NUM2INT(rb_ary_entry(argv[0], 5));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 6:
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      dw = NUM2INT(argv[4]);
      dh = NUM2INT(argv[5]);
      break;
    default:
      rb_raise(rb_eTypeError,"Invalid argument count (not 1 or 6)");
  }
  
  GET_AND_CHECK_IMAGE(self, old_im);
  imlib_context_set_image(old_im->im);
  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_create_cropped_scaled_image(x, y, w, h, dw, dh);
  im_o = Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);

  return im_o;
}

/*
 * Crop and scale an image
 *
 * Examples:
 *   iw, ih = image.width, image.height
 *   new_w, new_h = iw - 20, ih - 20
 *   x, y, w, h = 10, 10, iw - 10, ih - 10
 *   image.crop_scaled! x, y, w, h, new_w, new_h
 *
 *   iw, ih = image.width, image.height
 *   new_w, new_h = iw - 20, ih - 20
 *   values = [10, 10, iw - 10, iw - 10, new_w, new_h]
 *   image.crop_scaled! values
 *
 *   iw, ih = image.width, image.height
 *   new_w, new_h = iw - 20, ih - 20
 *   x, y, w, h = 10, 10, iw - 10, ih - 10
 *   image.create_crop_scaled! x, y, w, h, new_w, new_h
 *
 *   iw, ih = image.width, image.height
 *   new_w, new_h = iw - 20, ih - 20
 *   values = [10, 10, iw - 10, iw - 10, new_w, new_h]
 *   image.create_crop_scaled! values
 *
 */
static VALUE image_crop_scaled_inline(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  Imlib_Image old_im;
  int x = 0, y = 0, w = 0, h = 0, dw = 0, dh = 0;
  
  switch (argc) {
    case 1:
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          dw = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("dw")));
          dh = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("dh")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          dw = NUM2INT(rb_ary_entry(argv[0], 4));
          dh = NUM2INT(rb_ary_entry(argv[0], 5));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 6:
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      dw = NUM2INT(argv[4]);
      dh = NUM2INT(argv[5]);
      break;
    default:
      rb_raise(rb_eTypeError,"Invalid argument count (not 1 or 6)");
  }
  
  GET_AND_CHECK_IMAGE(self, im);
  old_im = im->im;
  imlib_context_set_image(old_im);
  im->im = imlib_create_cropped_scaled_image(x, y, w, h, dw, dh);
  imlib_context_set_image(old_im);
  imlib_free_image();

  return self;
}

/* 
 * Create a horizontally-flipped copy of an image
 *
 * Examples:
 *   backwards_image = old_image.flip_horizontal
 *
 */
static VALUE image_flip_horizontal(VALUE self) {
  ImStruct *im, *new_im;
  VALUE im_o;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  im_o = Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);

  imlib_context_set_image(new_im->im);
  imlib_image_flip_horizontal();

  return im_o;
}

/* 
 * Flip an image horizontally
 *
 * Examples:
 *   image.flip_horizontal!
 *
 */
static VALUE image_flip_horizontal_inline(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_flip_horizontal();

  return self;
}

/* 
 * Create a vertically-flipped copy of an image
 *
 * Examples:
 *   upside_down_image = old_image.flip_vertical
 *
 */
static VALUE image_flip_vertical(VALUE self) {
  ImStruct *im, *new_im;
  VALUE im_o;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  im_o = Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);

  imlib_context_set_image(new_im->im);
  imlib_image_flip_vertical();

  return im_o;
}

/* 
 * Flip an image vertically
 *
 * Examples:
 *   image.flip_vertical!
 *
 */
static VALUE image_flip_vertical_inline(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_flip_vertical();

  return self;
}

/* 
 * Create a copy of an image flipped along it's diagonal axis
 *
 * Examples:
 *   new_image = old_image.flip_diagonal
 *
 */
static VALUE image_flip_diagonal(VALUE self) {
  ImStruct *im, *new_im;
  VALUE im_o;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  im_o = Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);

  imlib_context_set_image(new_im->im);
  imlib_image_flip_diagonal();

  return im_o;
}

/* 
 * Flip an image along it's diagonal axis
 *
 * Examples:
 *   image.flip_diagonal!
 *
 */
static VALUE image_flip_diagonal_inline(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_flip_diagonal();

  return self;
}

/*
 * Return a copy of an image rotated in 90 degree increments
 *
 * Examples:
 *   increments = 3 # 90 * 3 degrees (eg 270 degrees)
 *   new_image = old_image.orientate increments
 *
 */
static VALUE image_orientate(VALUE self, VALUE val) {
  ImStruct *im, *new_im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  imlib_context_set_image(new_im->im);
  imlib_image_orientate(NUM2INT(val));
  return Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);
}

/*
 * Rotate an image in 90 degree increments
 *
 * Examples:
 *   increments = 3 # 90 * 3 degrees (eg 270 degrees)
 *   image.orientate! increments
 *
 */
static VALUE image_orientate_inline(VALUE self, VALUE val) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_orientate(NUM2INT(val));

  return self;
}

/* 
 * Return a blurred copy of an image
 * 
 * Examples:
 *   radius = 20 # radius of blur, in pixels
 *   new_image = old_image.blur radius
 *
 */
static VALUE image_blur(VALUE self, VALUE val) {
  ImStruct *im, *new_im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  imlib_context_set_image(new_im->im);
  imlib_image_blur(NUM2INT(val));
  return Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);
}

/* 
 * Blur an image
 * 
 * Examples:
 *   radius = 20 # radius of blur, in pixels
 *   image.blur! radius
 *
 */
static VALUE image_blur_inline(VALUE self, VALUE val) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_blur(NUM2INT(val));

  return self;
}

/* 
 * Return a sharpened copy of an image
 * 
 * Examples:
 *   radius = 15 # radius of sharpen, in pixels
 *   new_image = old_image.sharpen radius
 *
 */
static VALUE image_sharpen(VALUE self, VALUE val) {
  ImStruct *im, *new_im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  imlib_context_set_image(new_im->im);
  imlib_image_sharpen(NUM2INT(val));
  return Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);
}

/* 
 * Sharpened an image
 * 
 * Examples:
 *   radius = 15 # radius of sharpen, in pixels
 *   image.sharpen! radius
 *
 */
static VALUE image_sharpen_inline(VALUE self, VALUE val) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_sharpen(NUM2INT(val));

  return self;
}

/* 
 * Return a copy of an image suitable for seamless horizontal tiling
 * 
 * Examples:
 *   horiz_tile = old_image.tile_horizontal
 *
 */
static VALUE image_tile_horizontal(VALUE self) {
  ImStruct *im, *new_im;
  VALUE im_o;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  im_o = Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);

  imlib_context_set_image(new_im->im);
  imlib_image_tile_horizontal();

  return im_o;
}

/* 
 * Modify an image so it is suitable for seamless horizontal tiling
 * 
 * Examples:
 *   image.tile_horizontal!
 *
 */
static VALUE image_tile_horizontal_inline(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_tile_horizontal();

  return self;
}

/* 
 * Return a copy of an image suitable for seamless vertical tiling
 * 
 * Examples:
 *   vert_tile = old_image.tile_vertical
 *
 */
static VALUE image_tile_vertical(VALUE self) {
  ImStruct *im, *new_im;
  VALUE im_o;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  im_o = Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);

  imlib_context_set_image(new_im->im);
  imlib_image_tile_vertical();

  return im_o;
}

/* 
 * Modify an image so it is suitable for seamless vertical tiling
 * 
 * Examples:
 *   image.tile_vertical!
 *
 */
static VALUE image_tile_vertical_inline(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_tile_vertical();

  return self;
}

/* 
 * Return a copy of an image suitable for seamless tiling
 * 
 * Examples:
 *   horiz_tile = old_image.tile
 *
 */
static VALUE image_tile(VALUE self) {
  ImStruct *im, *new_im;
  VALUE im_o;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  im_o = Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);

  imlib_context_set_image(new_im->im);
  imlib_image_tile();

  return im_o;
}

/* 
 * Modify an image so it is suitable for seamless tiling
 * 
 * Examples:
 *   image.tile!
 *
 */
static VALUE image_tile_inline(VALUE self) {
  ImStruct *im;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_tile();

  return self;
}

/*
 * Clear the contents of an image
 *
 * Examples:
 *   image.clear
 *
 */
static VALUE image_clear(VALUE self) {
  ImStruct *im;
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_clear();

  return self;
}

/*
 * I'm honestly not quite sure what this function does, but I wrapped it
 * anyway.
 *
 */
static VALUE image_clear_color(VALUE self, VALUE rgba_color) {
  ImStruct  *im, *new_im;
  Imlib_Color *color;
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  imlib_context_set_image(new_im->im);

  Data_Get_Struct(rgba_color, Imlib_Color, color);
  imlib_image_clear_color(color->red, color->blue, color->green, color->alpha);

  return Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);
}

/*
 * I'm honestly not quite sure what this function does, but I wrapped it
 * anyway.
 *
 */
static VALUE image_clear_color_inline(VALUE self, VALUE rgba_color) {
  ImStruct  *im;
  Imlib_Color *color;
  
  GET_AND_CHECK_IMAGE(self, im);
  Data_Get_Struct(rgba_color, Imlib_Color, color);
  imlib_context_set_image(im->im);
  imlib_image_clear_color(color->red, color->blue, color->green, color->alpha);

  return self;
}

/* 
 * Draw a pixel at the specified coordinates.
 *
 * Note: Versions of Imlib2 up to and including 1.0.5 had a broken
 * imlib_image_draw_pixel() call. Imlib2-Ruby has a work-around, which
 * simulates drawing a pixel with a 1x1 rectangle.  To disable this
 * behavior, see the Imlib2::Image::draw_pixel_workaround= method.
 *
 * Examples:
 *   im.draw_pixel 10, 10                      # draw using context color
 *   im.draw_pixel 10, 10, Imlib2::Color::BLUE # draw blue pixel
 *   im.draw_pixel [10, 10], Imlib2::Color::RED # draw red pixel
 *
 */
static VALUE image_draw_pixel(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  VALUE color = Qnil;
  int x = 0, y = 0;
  int blend, aa;

  switch (argc) {
    case 1:
      /* one argument is an array or hash of points, with color
       * defaulting to Qnil (ie, the context color) */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 2:
      /* two arguments is either two fixnum points (with color
       * defaulting to Qnil), or an array or hash of points and a color
       * */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          color = argv[1];
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          color = argv[1];
          break;
        case T_FIXNUM:
          x = NUM2INT(argv[0]);
          y = NUM2INT(argv[1]);
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three arguments is two fixnum points and a color value */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      color = argv[2];
      break;
    default:
      rb_raise(rb_eTypeError,"Invalid argument count (not 1, 2, or 3)");
  }
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  if (color != Qnil)
    set_context_color(color);

  if (draw_pixel_workaround) {
    /* use draw_pixel workaround */

    /*********************************************************************/
    /* WORKAROUND: workaround for borked Imlib2 imlib_image_draw_pixel() */
    /*********************************************************************/
  
    /* save context, then disable blending and aa */
    blend = imlib_context_get_blend();
    aa = imlib_context_get_anti_alias();
  
    /* draw 1x1 rectangle */
    imlib_image_draw_rectangle(x, y, 1, 1);
    
    /* restore blend and aa context */
    imlib_context_set_blend(blend);
    imlib_context_set_anti_alias(aa);
  } else {
    /* use imlib_image_draw_pixel() (buggy for Imlib2 <= 1.0.5) */

    (void) imlib_image_draw_pixel(x, y, 0);
  }

  return self;
}

/* 
 * Draw a line at the specified coordinates.
 *
 * Examples:
 *   # draw line from 10, 10 to 20, 20 using context color
 *   im.draw_line 10, 10, 20, 20
 *
 *   # draw magenta line from 5, 10 to 15, 20
 *   im.draw_line 5, 10, 15, 20, Imlib2::Color::MAGENTA
 *
 *   # draw line from 10, 15 to 20, 25 using context color
 *   im.draw_pixel [10, 15], [20, 25]
 *
 *   # draw line from 1000, 2000 to 2100, 4200 with funky color
 *   my_color = Imlib2::Color::CmykColor.new 100, 255, 0, 128
 *   im.draw_line [1000, 2000], [2100, 4200], my_color
 *
 */
static VALUE image_draw_line(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  VALUE color = Qnil;
  int i = 0, x[2] = {0, 0}, y[2] = {0, 0};

  switch (argc) {
    case 2:
      /* two arguments is an array or hash of points, with color
       * defaulting to Qnil (ie, the context color) */
      for (i = 0; i < 2; i++) {
        switch (TYPE(argv[i])) {
          case T_HASH:
            x[i] = NUM2INT(rb_hash_aref(argv[i], rb_str_new2("x")));
            y[i] = NUM2INT(rb_hash_aref(argv[i], rb_str_new2("y")));
            break;
          case T_ARRAY:
            x[i] = NUM2INT(rb_ary_entry(argv[i], 0));
            y[i] = NUM2INT(rb_ary_entry(argv[i], 1));
            break;
          default:
            rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
        }
      }
      break;
    case 3:
      /* three arguments is two arrays or hashes of points and a color
       * */
      for (i = 0; i < 2; i++) {
        switch (TYPE(argv[i])) {
          case T_HASH:
            x[i] = NUM2INT(rb_hash_aref(argv[i], rb_str_new2("x")));
            y[i] = NUM2INT(rb_hash_aref(argv[i], rb_str_new2("y")));
            break;
          case T_ARRAY:
            x[i] = NUM2INT(rb_ary_entry(argv[i], 0));
            y[i] = NUM2INT(rb_ary_entry(argv[i], 1));
            break;
          default:
            rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
        }
      }
      color = argv[2];
      break;
    case 5:
      /* 5 arguments is 4 fixnum points and a color value */
      color = argv[4];

      /* pass-through */
    case 4:
      x[0] = NUM2INT(argv[0]);
      y[0] = NUM2INT(argv[1]);
      x[1] = NUM2INT(argv[2]);
      y[1] = NUM2INT(argv[3]);
      break;
    default:
      rb_raise(rb_eTypeError,"Invalid argument count (not 2, 3, 4, or 5)");
  }
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  if (color != Qnil)
    set_context_color(color);
  (void) imlib_image_draw_line(x[0], y[0], x[1], y[1], 0);
  
  return self;
}


/* 
 * Draw a rectangle outline at the specified coordinates.
 *
 * Examples:
 *   # draw rectangle around edge of image using context color
 *   rect = [1, 1, im.width - 2, im.height - 2]
 *   im.draw_rect rect
 *
 *   # draw magenta rectangle outline in top-left corner of image
 *   color = Imlib2::Color::MAGENTA
 *   im.draw_rect [0, 0], [im.width / 2, im.height / 2], color
 *
 *   # draw square from 10, 10 to 30, 30 using context color
 *   im.draw_rect [10, 10, 20, 20] 
 *
 */
static VALUE image_draw_rect(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  VALUE color = Qnil;
  int x, y, w, h;

  x = y = w = h = 0;
  switch (argc) {
    case 1:
      /* 1 argument is an array or hash of x, y, w, h with color
       * defaulting to Qnil (ie, the context color) */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 2:
      /* two arguments is an array or hash of x, y, w, h with a color,
       * or an array or hash of x, y, and an array or hash of w, h (with
       * color defaulting to Qnil (ie, the context color) */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));
              break;
            default:
              x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
              y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
              /* we could do a type check here, but if it's invalid
               * it'll get caught in the set_context_color() call */
              color = argv[1];
          }
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));
              break;
            default:
              w = NUM2INT(rb_ary_entry(argv[0], 2));
              h = NUM2INT(rb_ary_entry(argv[0], 3));
              /* we could do a type check here, but if it's invalid
               * it'll get caught in the set_context_color() call */
              color = argv[1];
          }
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three arguments is an array or hash of x, y and a color */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      switch (TYPE(argv[1])) {
        case T_HASH:
          w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          break;
        case T_ARRAY:
          w = NUM2INT(rb_ary_entry(argv[1], 0));
          h = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      color = argv[2];
      break;
    case 4:
      /* 4 arguments is x, y, w, y (color to Qnil) */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      break;
    case 5:
      /* 4 arguments is x, y, w, y, color */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      color = argv[4];
      break;
    default:
      rb_raise(rb_eTypeError,"Invalid argument count (not 1, 2, 3, 4, or 5)");
  }
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  if (color != Qnil)
    set_context_color(color);
  imlib_image_draw_rectangle(x, y, w, h);
  
  return self;
}


/* 
 * Fill a rectangle at the specified coordinates.
 *
 * Examples:
 *   # fill image using context color
 *   rect = [0, 0, im.width, im.height]
 *   im.fill_rect rect
 *
 *   # fill top-left quarter of image with green
 *   color = Imlib2::Color::GREEN
 *   im.fill_rect [0, 0], [im.width / 2, im.height / 2], color
 *
 *   # fill square from 10, 10 to 30, 30 using context color
 *   im.fill_rect [10, 10, 20, 20] 
 *
 */
static VALUE image_fill_rect(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  VALUE color = Qnil;
  int x, y, w, h;

  x = y = w = h = 0;
  switch (argc) {
    case 1:
      /* 1 argument is an array or hash of x, y, w, h with color
       * defaulting to Qnil (ie, the context color) */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 2:
      /* two arguments is an array or hash of x, y, w, h with a color,
       * or an array or hash of x, y, and an array or hash of w, h (with
       * color defaulting to Qnil (ie, the context color) */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));
              break;
            default:
              x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
              y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
              /* we could do a type check here, but if it's invalid
               * it'll get caught in the set_context_color() call */
              color = argv[1];
          }
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));
              break;
            default:
              w = NUM2INT(rb_ary_entry(argv[0], 2));
              h = NUM2INT(rb_ary_entry(argv[0], 3));
              /* we could do a type check here, but if it's invalid
               * it'll get caught in the set_context_color() call */
              color = argv[1];
          }
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three arguments is an array or hash of x, y and a color */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      switch (TYPE(argv[1])) {
        case T_HASH:
          w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          break;
        case T_ARRAY:
          w = NUM2INT(rb_ary_entry(argv[1], 0));
          h = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      color = argv[2];
      break;
    case 4:
      /* 4 arguments is x, y, w, y (color to Qnil) */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      break;
    case 5:
      /* 4 arguments is x, y, w, y, color */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      color = argv[4];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 1, 2, 3, 4, or 5)");
  }
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  if (color != Qnil)
    set_context_color(color);
  imlib_image_fill_rectangle(x, y, w, h);
  
  return self;
}

/*
 * Copy the alpha channel from the source image to the specified coordinates
 *
 * Examples:
 *   image.copy_alpha source_image, 10, 10
 *   image.copy_alpha source_image, [10, 10]
 *
 */
static VALUE image_copy_alpha(int argc, VALUE *argv, VALUE self) {
  ImStruct *src_im, *im;
  VALUE src;
  int x, y;

  src = argv[0];
  switch (argc) {
    case 2:
      switch (TYPE(argv[1])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[1], 0));
          y = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      x = NUM2INT(argv[1]);
      y = NUM2INT(argv[2]);
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
  }

  GET_AND_CHECK_IMAGE(src, src_im);
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_copy_alpha_to_image(src_im->im, x, y);

  return self;
}

/*
 * Copy the alpha channel from a rectangle of the source image to the
 * specified coordinates
 *
 * Examples:
 *   x, y, w, h = 10, 20, 100, 200
 *   dest_x, dest_y = 5, 10
 *   image.copy_alpha_rect source_image, x, y, w, h, dest_x, dest_y
 *
 *   source_rect = [10, 20, 100, 200]
 *   dest_coords = [5, 10]
 *   image.copy_alpha_rect source_image, source_rect, dest_coords
 *
 *   values = [10, 20, 100, 200, 5, 10]
 *   image.copy_alpha_rect source_image, values
 *
 */
static VALUE image_copy_alpha_rect(int argc, VALUE *argv, VALUE self) {
  ImStruct *src_im, *im;
  VALUE src;
  int x, y, w, h, dx, dy;

  x = y = w = h = dx = dy = 0;
  src = argv[0];
  switch (argc) {
    case 2:
      /* 2 arguments is a source image and an array or hash of
       * x, y, w, h, dx, dy */
      switch (TYPE(argv[1])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          dx = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("dx")));
          dy = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("dy")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[1], 0));
          y = NUM2INT(rb_ary_entry(argv[1], 1));
          w = NUM2INT(rb_ary_entry(argv[1], 2));
          h = NUM2INT(rb_ary_entry(argv[1], 3));
          dx = NUM2INT(rb_ary_entry(argv[1], 4));
          dy = NUM2INT(rb_ary_entry(argv[1], 5));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three arguments is a source image, an array or hash of x,y,w,h,
       * and an array or hash of dx, dy */
      switch (TYPE(argv[1])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[1], 0));
          y = NUM2INT(rb_ary_entry(argv[1], 1));
          w = NUM2INT(rb_ary_entry(argv[1], 2));
          h = NUM2INT(rb_ary_entry(argv[1], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      switch (TYPE(argv[2])) {
        case T_HASH:
          dx = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("dx")));
          dy = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("dy")));
          break;
        case T_ARRAY:
          dx = NUM2INT(rb_ary_entry(argv[1], 0));
          dy = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 4:
      /* four arguments is a source image, an array or hash of [x, y],
       * an array or hash of [w, h], and an array or hash of [dx, dy],
       * or a source image, an array or hash of [x, y, w, h], dx, dy */
      switch (TYPE(argv[1])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));

          switch (TYPE(argv[2])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("h")));

              switch (TYPE(argv[3])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[3], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[3], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[3], 0));
                  dy = NUM2INT(rb_ary_entry(argv[3], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type " 
                                          "(not array or hash)");
              }
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[2], 0));
              h = NUM2INT(rb_ary_entry(argv[2], 1));

              switch (TYPE(argv[3])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[3], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[3], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[3], 0));
                  dy = NUM2INT(rb_ary_entry(argv[3], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type " 
                                          "(not array or hash)");
              }
              break;
            default:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
              dx = NUM2INT(argv[2]);
              dy = NUM2INT(argv[3]);
          }
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[1], 0));
          y = NUM2INT(rb_ary_entry(argv[1], 1));

          switch (TYPE(argv[2])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("h")));

              switch (TYPE(argv[3])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[3], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[3], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[3], 0));
                  dy = NUM2INT(rb_ary_entry(argv[3], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type " 
                                          "(not array or hash)");
              }
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[2], 0));
              h = NUM2INT(rb_ary_entry(argv[2], 1));

              switch (TYPE(argv[3])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[3], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[3], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[3], 0));
                  dy = NUM2INT(rb_ary_entry(argv[3], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type " 
                                          "(not array or hash)");
              }
              break;
            default:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
              dx = NUM2INT(argv[2]);
              dy = NUM2INT(argv[3]);
          }
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 5:
      /* five arguments is a source image, an array or hash of [x, y],
       * an array or hash of [w, h], dx, dy */
      switch (TYPE(argv[1])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[1], 0));
          y = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      switch (TYPE(argv[2])) {
        case T_HASH:
          w = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("h")));
          break;
        case T_ARRAY:
          w = NUM2INT(rb_ary_entry(argv[2], 0));
          h = NUM2INT(rb_ary_entry(argv[2], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      dx = NUM2INT(argv[3]);
      dy = NUM2INT(argv[4]);
    case 6:
      /* six arguments is a source image, x, y, w, h, and an array or
       * hash of [dx, dy], or source image, an array or hash of [x, y],
       * an array or hash of [w, h], dx, dy */
      switch (TYPE(argv[1])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));

          switch (TYPE(argv[2])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("h")));
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[2], 0));
              h = NUM2INT(rb_ary_entry(argv[2], 1));
              break;
            default:
              rb_raise(rb_eTypeError, "Invalid argument type "
                                      "(not array or hash)");
          }
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[1], 0));
          y = NUM2INT(rb_ary_entry(argv[1], 1));

          switch (TYPE(argv[2])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("h")));
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[2], 0));
              h = NUM2INT(rb_ary_entry(argv[2], 1));
              break;
            default:
              rb_raise(rb_eTypeError, "Invalid argument type "
                                      "(not array or hash)");
          }
          break;
        default:
          x = NUM2INT(argv[1]);
          y = NUM2INT(argv[2]);
          w = NUM2INT(argv[3]);
          h = NUM2INT(argv[4]);
      }
      break;
    case 7:
      /* seven arguments is a source image, x, y, w, y, dx, dy */
      x = NUM2INT(argv[1]);
      y = NUM2INT(argv[2]);
      w = NUM2INT(argv[3]);
      h = NUM2INT(argv[4]);
      dx = NUM2INT(argv[5]);
      dy = NUM2INT(argv[6]);
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count "
                              "(not 2, 3, 4, 5, 6, or 7)");
  }
  
  GET_AND_CHECK_IMAGE(src, src_im);
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_copy_alpha_rectangle_to_image(src_im->im, x, y, w, h, dx, dy);
  
  return self;
}

/*
 * Scroll a rectangle to the specified coordinates
 *
 * Examples:
 *   x, y, w, h = 10, 20, 100, 200
 *   dest_x, dest_y = 5, 10
 *   image.scroll_rect x, y, w, h, dest_x, dest_y
 *
 *   source_rect = [10, 20, 100, 200]
 *   dest_coords = [5, 10]
 *   image.scroll_rect source_rect, dest_coords
 *
 *   values = [10, 20, 100, 200, 5, 10]
 *   image.scroll_rect values
 *
 */
static VALUE image_scroll_rect(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  int x, y, w, h, dx, dy;

  switch (argc) {
    case 1:
      /* one argument is an array or hash of [x, y, w, h, dx, dy] */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          dx = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("dx")));
          dy = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("dy")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          dx = NUM2INT(rb_ary_entry(argv[0], 4));
          dy = NUM2INT(rb_ary_entry(argv[0], 5));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 2:
      /* two arguments is an array or hash of [x, y, w, h] and an array
       * or hash of [dx, dy] */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      switch (TYPE(argv[1])) {
        case T_HASH:
          dx = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("dx")));
          dy = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("dy")));
          break;
        case T_ARRAY:
          dx = NUM2INT(rb_ary_entry(argv[1], 0));
          dy = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three arguments is an array or hash of [x, y], an array or hash
       * of [w, h] and an array or hash of [dx, dy], or an array or hash
       * of [x, y, w, h], dx, dy */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));

          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));

              switch (TYPE(argv[2])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[2], 0));
                  dy = NUM2INT(rb_ary_entry(argv[2], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type "
                                          "(not array or hash)");
              }
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));

              switch (TYPE(argv[2])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[2], 0));
                  dy = NUM2INT(rb_ary_entry(argv[2], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type "
                                          "(not array or hash)");
              }
              break;
            default:
              w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
              dx = NUM2INT(argv[1]);
              dy = NUM2INT(argv[2]);
          }
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));

          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));

              switch (TYPE(argv[2])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[2], 0));
                  dy = NUM2INT(rb_ary_entry(argv[2], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type "
                                          "(not array or hash)");
              }
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));

              switch (TYPE(argv[2])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[2], 0));
                  dy = NUM2INT(rb_ary_entry(argv[2], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type "
                                          "(not array or hash)");
              }
              break;
            default:
              w = NUM2INT(rb_ary_entry(argv[0], 2));
              h = NUM2INT(rb_ary_entry(argv[0], 3));
              dx = NUM2INT(argv[1]);
              dy = NUM2INT(argv[2]);
          }
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 4:
      /* four arguments is an array or hash of [x, y], an array or hash
       * of [w, h], dx, dy */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      switch (TYPE(argv[1])) {
        case T_HASH:
          w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          break;
        case T_ARRAY:
          w = NUM2INT(rb_ary_entry(argv[1], 0));
          h = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      dx = NUM2INT(argv[2]);
      dy = NUM2INT(argv[3]);
      break;
    case 5:
      /* five arguments is x, y, w, h, and an array or hash of
       * [dx, dy] */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      switch (TYPE(argv[4])) {
        case T_HASH:
          dx = NUM2INT(rb_hash_aref(argv[4], rb_str_new2("dx")));
          dy = NUM2INT(rb_hash_aref(argv[4], rb_str_new2("dy")));
          break;
        case T_ARRAY:
          dx = NUM2INT(rb_ary_entry(argv[4], 0));
          dy = NUM2INT(rb_ary_entry(argv[4], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 6:
      /* six arguments is x, y, w, h, dx, dy */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      dx = NUM2INT(argv[4]);
      dy = NUM2INT(argv[5]);
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count "
                              "(not 2, 3, 4, 5, 6, or 7)");
  }

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_scroll_rect(x, y, w, h, dx, dy);

  return self;
}

/*
 * Copy a rectangle to the specified coordinates
 *
 * Examples:
 *   x, y, w, h = 10, 20, 100, 200
 *   dest_x, dest_y = 5, 10
 *   image.copy_rect x, y, w, h, dest_x, dest_y
 *
 *   source_rect = [10, 20, 100, 200]
 *   dest_coords = [5, 10]
 *   image.copy_rect source_rect, dest_coords
 *
 *   values = [10, 20, 100, 200, 5, 10]
 *   image.copy_rect values
 *
 */
static VALUE image_copy_rect(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  int x, y, w, h, dx, dy;

  switch (argc) {
    case 1:
      /* one argument is an array or hash of [x, y, w, h, dx, dy] */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          dx = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("dx")));
          dy = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("dy")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          dx = NUM2INT(rb_ary_entry(argv[0], 4));
          dy = NUM2INT(rb_ary_entry(argv[0], 5));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 2:
      /* two arguments is an array or hash of [x, y, w, h] and an array
       * or hash of [dx, dy] */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      switch (TYPE(argv[1])) {
        case T_HASH:
          dx = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("dx")));
          dy = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("dy")));
          break;
        case T_ARRAY:
          dx = NUM2INT(rb_ary_entry(argv[1], 0));
          dy = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three arguments is an array or hash of [x, y], an array or hash
       * of [w, h] and an array or hash of [dx, dy], or an array or hash
       * of [x, y, w, h], dx, dy */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));

          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));

              switch (TYPE(argv[2])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[2], 0));
                  dy = NUM2INT(rb_ary_entry(argv[2], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type "
                                          "(not array or hash)");
              }
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));

              switch (TYPE(argv[2])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[2], 0));
                  dy = NUM2INT(rb_ary_entry(argv[2], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type "
                                          "(not array or hash)");
              }
              break;
            default:
              w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
              dx = NUM2INT(argv[1]);
              dy = NUM2INT(argv[2]);
          }
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));

          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));

              switch (TYPE(argv[2])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[2], 0));
                  dy = NUM2INT(rb_ary_entry(argv[2], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type "
                                          "(not array or hash)");
              }
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));

              switch (TYPE(argv[2])) {
                case T_HASH:
                  dx = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dx")));
                  dy = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("dy")));
                  break;
                case T_ARRAY:
                  dx = NUM2INT(rb_ary_entry(argv[2], 0));
                  dy = NUM2INT(rb_ary_entry(argv[2], 1));
                  break;
                default:
                  rb_raise(rb_eTypeError, "Invalid argument type "
                                          "(not array or hash)");
              }
              break;
            default:
              w = NUM2INT(rb_ary_entry(argv[0], 2));
              h = NUM2INT(rb_ary_entry(argv[0], 3));
              dx = NUM2INT(argv[1]);
              dy = NUM2INT(argv[2]);
          }
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 4:
      /* four arguments is an array or hash of [x, y], an array or hash
       * of [w, h], dx, dy */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      switch (TYPE(argv[1])) {
        case T_HASH:
          w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          break;
        case T_ARRAY:
          w = NUM2INT(rb_ary_entry(argv[1], 0));
          h = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      dx = NUM2INT(argv[2]);
      dy = NUM2INT(argv[3]);
      break;
    case 5:
      /* five arguments is x, y, w, h, and an array or hash of
       * [dx, dy] */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      switch (TYPE(argv[4])) {
        case T_HASH:
          dx = NUM2INT(rb_hash_aref(argv[4], rb_str_new2("dx")));
          dy = NUM2INT(rb_hash_aref(argv[4], rb_str_new2("dy")));
          break;
        case T_ARRAY:
          dx = NUM2INT(rb_ary_entry(argv[4], 0));
          dy = NUM2INT(rb_ary_entry(argv[4], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 6:
      /* six arguments is x, y, w, h, dx, dy */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      dx = NUM2INT(argv[4]);
      dy = NUM2INT(argv[5]);
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count "
                              "(not 2, 3, 4, 5, 6, or 7)");
  }

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  imlib_image_copy_rect(x, y, w, h, dx, dy);

  return self;
}

/*
 * Draw an ellipse at the specified coordinates with the given color
 *
 * Examples:
 *   # draw an ellipse in the center of the image using the context color
 *   xc, yc, w, h = image.w / 2, image.h / 2, image.w / 2, image.h / 2
 *   image.draw_oval xc, yc, w, h
 *
 *   # draw a violet circle in the center of the image
 *   rect = [image.w / 2, image.h / 2, image.w / 2, image.w / 2]
 *   color = Imlib2::Color::VIOLET
 *   image.draw_ellipse rect, color
 *
 */
static VALUE image_draw_ellipse(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  VALUE color = Qnil;
  int x, y, w, h;

  x = y = w = h = 0;
  switch (argc) {
    case 1:
      /* 1 argument is an array or hash of x, y, w, h with color
       * defaulting to Qnil (ie, the context color) */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 2:
      /* two arguments is an array or hash of x, y, w, h with a color,
       * or an array or hash of x, y, and an array or hash of w, h (with
       * color defaulting to Qnil (ie, the context color) */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));
              break;
            default:
              x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
              y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
              /* we could do a type check here, but if it's invalid
               * it'll get caught in the set_context_color() call */
              color = argv[1];
          }
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));
              break;
            default:
              w = NUM2INT(rb_ary_entry(argv[0], 2));
              h = NUM2INT(rb_ary_entry(argv[0], 3));
              /* we could do a type check here, but if it's invalid
               * it'll get caught in the set_context_color() call */
              color = argv[1];
          }
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three arguments is an array or hash of x, y and a color */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      switch (TYPE(argv[1])) {
        case T_HASH:
          w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          break;
        case T_ARRAY:
          w = NUM2INT(rb_ary_entry(argv[1], 0));
          h = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      color = argv[2];
      break;
    case 4:
      /* 4 arguments is x, y, w, y (color to Qnil) */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      break;
    case 5:
      /* 4 arguments is x, y, w, y, color */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      color = argv[4];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 1, 2, 3, 4, or 5)");
  }
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  if (color != Qnil)
    set_context_color(color);
  imlib_image_draw_ellipse(x, y, w, h);
  
  return self;
}

/*
 * Fill an ellipse at the specified coordinates with the given color
 *
 * Examples:
 *   # fill an ellipse in the center of the image using the context color
 *   xc, yc, w, h = image.w / 2, image.h / 2, image.w / 2, image.h / 2
 *   image.draw_oval xc, yc, w, h
 *
 *   # fill a violet circle in the center of the image
 *   rect = [image.w / 2, image.h / 2, image.w / 2, image.w / 2]
 *   color = Imlib2::Color::VIOLET
 *   image.draw_ellipse rect, color
 *
 */
static VALUE image_fill_ellipse(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  VALUE color = Qnil;
  int x, y, w, h;

  x = y = w = h = 0;
  switch (argc) {
    case 1:
      /* 1 argument is an array or hash of x, y, w, h with color
       * defaulting to Qnil (ie, the context color) */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          w = NUM2INT(rb_ary_entry(argv[0], 2));
          h = NUM2INT(rb_ary_entry(argv[0], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 2:
      /* two arguments is an array or hash of x, y, w, h with a color,
       * or an array or hash of x, y, and an array or hash of w, h (with
       * color defaulting to Qnil (ie, the context color) */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));
              break;
            default:
              x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
              y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
              /* we could do a type check here, but if it's invalid
               * it'll get caught in the set_context_color() call */
              color = argv[1];
          }
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          switch (TYPE(argv[1])) {
            case T_HASH:
              w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
              h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
              break;
            case T_ARRAY:
              w = NUM2INT(rb_ary_entry(argv[1], 0));
              h = NUM2INT(rb_ary_entry(argv[1], 1));
              break;
            default:
              w = NUM2INT(rb_ary_entry(argv[0], 2));
              h = NUM2INT(rb_ary_entry(argv[0], 3));
              /* we could do a type check here, but if it's invalid
               * it'll get caught in the set_context_color() call */
              color = argv[1];
          }
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three arguments is an array or hash of x, y and a color */
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      switch (TYPE(argv[1])) {
        case T_HASH:
          w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          break;
        case T_ARRAY:
          w = NUM2INT(rb_ary_entry(argv[1], 0));
          h = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      color = argv[2];
      break;
    case 4:
      /* 4 arguments is x, y, w, y (color to Qnil) */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      break;
    case 5:
      /* 4 arguments is x, y, w, y, color */
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      w = NUM2INT(argv[2]);
      h = NUM2INT(argv[3]);
      color = argv[4];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 1, 2, 3, 4, or 5)");
  }
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  if (color != Qnil)
    set_context_color(color);
  imlib_image_fill_ellipse(x, y, w, h);
  
  return self;
}

/*
 * Blend a source image onto the image
 *
 * Examples:
 *   src_x, src_y, src_w, src_h = 10, 10, 100, 100
 *   dst_x, dst_y, dst_w, dst_h = 10, 10, 50, 50
 *   image.blend! source_image,
 *               src_x, src_y, src_w, src_h, 
 *               dst_x, dst_y, dst_w, dst_h
 *
 *   src_rect = [50, 50, 5, 5]
 *   dst_rect = [0, 0, image.width, image.height]
 *   merge_alpha = false
 *   image.blend! source_image, src_rect, dst_rect, merge_alpha
 *
 *   src_x, src_y, src_w, src_h = 10, 10, 100, 100
 *   dst_x, dst_y, dst_w, dst_h = 10, 10, 50, 50
 *   image.blend_image! source_image,
 *                      src_x, src_y, src_w, src_h, 
 *                      dst_x, dst_y, dst_w, dst_h
 *
 *   src_rect = [50, 50, 5, 5]
 *   dst_rect = [0, 0, image.width, image.height]
 *   merge_alpha = false
 *   image.blend_image! source_image, src_rect, dst_rect, merge_alpha
 *
 */
static VALUE image_blend_image_inline(int argc, VALUE *argv, VALUE self) {
  ImStruct *im, *src_im;
  int i, s[4], d[4];
  char merge_alpha = 1;
  
  switch (argc) {
    case 4:
      /* four arguments is source image, an array or hash of
       * [sx, sy, sw, sh], an array or hash of [sx, sy, sw, sh], and
       * merge_alpha */
      merge_alpha = (argv[3] == Qtrue) ? 1 : 0;
    case 3:
      /* three arguments is source image, an array or hash of
       * [sx, sy, sw, sh], and an array or hash of [sx, sy, sw, sh], 
       * with merge_alpha defaulting to true */
      switch (TYPE(argv[1])) {
        case T_HASH:
          s[0] = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          s[1] = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));
          s[2] = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          s[3] = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          break;
        case T_ARRAY:
          for (i = 0; i < 4; i++)
            s[i] = NUM2INT(rb_ary_entry(argv[1], i));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }

      switch (TYPE(argv[2])) {
        case T_HASH:
          d[0] = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("x")));
          d[1] = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("y")));
          d[2] = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("w")));
          d[3] = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("h")));
          break;
        case T_ARRAY:
          for (i = 0; i < 4; i++)
            d[i] = NUM2INT(rb_ary_entry(argv[2], i));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      
      break;
    case 6:
      /* six arguments is source image, an array or hash of [sx, sy], an
       * array or hash of [sw, sh], an array or hash of [dx, dy], an
       * array or hash of [dw, dh], and merge_alpha */
      merge_alpha = (argv[5] == Qtrue) ? 1 : 0;
    case 5:
      /* five arguments is source image, an array or hash of [sx, sy], an
       * array or hash of [sw, sh], an array or hash of [dx, dy], and an
       * array or hash of [dw, dh], with merge_alpha defaulting to true */

      switch (TYPE(argv[1])) {
        case T_HASH:
          s[0] = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          s[1] = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));
          break;
        case T_ARRAY:
          for (i = 0; i < 2; i++)
            s[i] = NUM2INT(rb_ary_entry(argv[1], i));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }

      switch (TYPE(argv[2])) {
        case T_HASH:
          s[2] = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("s")));
          s[3] = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("h")));
          break;
        case T_ARRAY:
          for (i = 0; i < 2; i++)
            s[i + 2] = NUM2INT(rb_ary_entry(argv[2], i));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }

      switch (TYPE(argv[3])) {
        case T_HASH:
          d[0] = NUM2INT(rb_hash_aref(argv[3], rb_str_new2("x")));
          d[1] = NUM2INT(rb_hash_aref(argv[3], rb_str_new2("y")));
          break;
        case T_ARRAY:
          for (i = 0; i < 2; i++)
            d[i] = NUM2INT(rb_ary_entry(argv[3], i));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }

      switch (TYPE(argv[4])) {
        case T_HASH:
          d[2] = NUM2INT(rb_hash_aref(argv[4], rb_str_new2("s")));
          d[3] = NUM2INT(rb_hash_aref(argv[4], rb_str_new2("h")));
          break;
        case T_ARRAY:
          for (i = 0; i < 2; i++)
            d[i + 2] = NUM2INT(rb_ary_entry(argv[4], i));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }

      break;
    case 10:
      /* ten arguments is source image, sx, sy, sw, sh, dx, dy, dw, dh,
       * and merge_alpha */
      merge_alpha = (argv[9] == Qtrue) ? 1 : 0;
    case 9:
      /* ten arguments is source image, sx, sy, sw, sh, dx, dy, dw, and
       * dh, with merge_alpha defaulting to true */

      for (i = 0; i < 4; i++)
        s[i] = NUM2INT(argv[i +  1]);
      for (i = 0; i < 4; i++)
        d[i] = NUM2INT(argv[i +  5]);
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count "
                              "(not 3, 4, 5, 6, 9, or 10)");
  }

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  GET_AND_CHECK_IMAGE(argv[0], src_im);
  imlib_blend_image_onto_image(src_im->im, merge_alpha,
                               s[0], s[1], s[2], s[3], 
                               d[0], d[1], d[2], d[3]);
  
  return self;
}

/*
 * Return a copy of the image with the a portion of the source image
 * blended at the specified rectangle.
 *
 * Examples:
 *   src_x, src_y, src_w, src_h = 10, 10, 100, 100
 *   dst_x, dst_y, dst_w, dst_h = 10, 10, 50, 50
 *   image.blend source_image,
 *               src_x, src_y, src_w, src_h, 
 *               dst_x, dst_y, dst_w, dst_h
 *
 *   src_rect = [50, 50, 5, 5]
 *   dst_rect = [0, 0, image.width, image.height]
 *   merge_alpha = false
 *   image.blend source_image, src_rect, dst_rect, merge_alpha
 *
 *   src_x, src_y, src_w, src_h = 10, 10, 100, 100
 *   dst_x, dst_y, dst_w, dst_h = 10, 10, 50, 50
 *   image.blend_image source_image,
 *                     src_x, src_y, src_w, src_h, 
 *                     dst_x, dst_y, dst_w, dst_h
 *
 *   src_rect = [50, 50, 5, 5]
 *   dst_rect = [0, 0, image.width, image.height]
 *   merge_alpha = false
 *   image.blend_image source_image, src_rect, dst_rect, merge_alpha
 *
 */
static VALUE image_blend_image(int argc, VALUE *argv, VALUE self) {
  ImStruct *im, *new_im;
  VALUE i_o;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  new_im = malloc(sizeof(ImStruct));
  new_im->im = imlib_clone_image();
  i_o = Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);

  return image_blend_image_inline(argc, argv, i_o);
}

/*
 * Return a rotated copy of the image
 *
 * Examples:
 *   new_image = old_image.rotate 37.2
 *
 */
static VALUE image_rotate(VALUE self, VALUE angle) {
  ImStruct *new_im, *im;
  double a;

  new_im = malloc(sizeof(ImStruct));

  GET_AND_CHECK_IMAGE(self, im);
  a = rb_float_new(angle);
  imlib_context_set_image(im->im);
  
  new_im->im = imlib_create_rotated_image(a);
  
  return Data_Wrap_Struct(cImage, 0, im_struct_free, new_im);
}

/*
 * Rotates the image
 *
 * Examples:
 *   image.rotate! 37.2
 *
 */
static VALUE image_rotate_inline(VALUE self, VALUE angle) {
  ImStruct *im;
  Imlib_Image new_im;
  double a;

  GET_AND_CHECK_IMAGE(self, im);
  a = rb_float_new(angle);
  imlib_context_set_image(im->im);
  
  new_im = imlib_create_rotated_image(a);
  
  imlib_context_set_image(im->im);
  imlib_free_image();

  im->im = new_im;

  return self;
}

/*
 * Draw a string with the given Imlib2::Font at the specified coordinates
 *
 * Examples:
 *   font = Imlib2::Font.new 'helvetica/12'
 *   string = 'the blue crow flies at midnight'
 *   image.draw_text font, string, 10, 10
 *
 *   # draw text in a specified color
 *   font = Imlib2::Font.new 'helvetica/12'
 *   string = 'the blue crow flies at midnight'
 *   color = Imlib2::Color::AQUA
 *   image.draw_text font, string, 10, 10, color
 *
 *   # draw text in a specified direction
 *   font = Imlib2::Font.new 'verdana/24'
 *   string = 'the blue crow flies at midnight'
 *   color = Imlib2::Color::YELLOW
 *   direction = Imlib2::Direction::DOWN
 *   image.draw_text font, string, 10, 10, color, direction
 *
 *   # draw text with return metrics
 *   font = Imlib2::Font.new 'arial/36'
 *   string = 'the blue crow flies at midnight'
 *   color = Imlib2::Color::PURPLE
 *   direction = Imlib2::Direction::LEFT
 *   metrics = image.draw_text font, string, 10, 10, color, direction
 *   ['width', 'height', 'horiz_advance', 'vert_advance'].each_index { |i, v|
 *     puts v << ' = ' << metrics[i]
 *   }
 *
 */ 
static VALUE image_draw_text(int argc, VALUE *argv, VALUE self) {
  ImStruct   *im;
  Imlib_Font *font;
  VALUE text, ary, color = Qnil, dir = Qnil;
  int x, y, i, r[] = { 0, 0, 0, 0 }, old_dir = -1;

  switch (argc) {
    case 3:
      /* three arguments is a font, a string, and an array or hash of
       * x, y, with both color and direction defaulting to Qnil (the
       * context values) */
      switch (TYPE(argv[2])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[2], 0));
          y = NUM2INT(rb_ary_entry(argv[2], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
      }
      break;
    case 4:
      /* four arguments is a font, a string, x, y, with color and
       * direction defaulting to Qnil, OR a font, a string, an array
       * or hash of [x, y] and either a color or a direction */
      switch (TYPE(argv[2])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("y")));

          if (FIXNUM_P(argv[3]))
            dir = argv[3];
          else 
            color = argv[3];
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[2], 0));
          y = NUM2INT(rb_ary_entry(argv[2], 1));
          break;

          if (FIXNUM_P(argv[3]))
            dir = argv[3];
          else 
            color = argv[3];
        default:
          x = NUM2INT(argv[2]);
          y = NUM2INT(argv[3]);
      }
      break;
    case 5:
      /* five arguments is a font, a string, x, y, a color, OR a font, a
       * string, an array or hash of [x, y], a color and a direction */
      if (FIXNUM_P(argv[2])) {
        x = NUM2INT(argv[2]);
        y = NUM2INT(argv[3]);
        color = argv[4];
      } else {
        switch (TYPE(argv[2])) {
          case T_HASH:
            x = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("x")));
            y = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("y")));
            break;
          case T_ARRAY:
            x = NUM2INT(rb_ary_entry(argv[2], 0));
            y = NUM2INT(rb_ary_entry(argv[2], 1));
            break;
          default:
            rb_raise(rb_eTypeError, "Invalid argument type "
                                    "(not Array or Hash)");
        }
        color = argv[3];
        dir = argv[4];
      }
      break;
    case 6:
      x = NUM2INT(argv[2]);
      y = NUM2INT(argv[3]);
      color = argv[4];
      dir = argv[5];
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 3, 4, or 5)");
  }

  Data_Get_Struct(argv[0], Imlib_Font, font);
  GET_AND_CHECK_IMAGE(self, im);
  text = argv[1];

  imlib_context_set_font(*font);
  imlib_context_set_image(im->im);

  if (color != Qnil) 
    set_context_color(color);
  if (dir != Qnil) {
    old_dir = imlib_context_get_direction();
    imlib_context_set_direction(NUM2INT(dir));
  }

  imlib_text_draw_with_return_metrics(x, y, StringValuePtr(text), 
                                      &r[0], &r[1], &r[2], &r[3]);
  if (dir != Qnil)
    imlib_context_set_direction(old_dir);

  ary = rb_ary_new();
  for (i = 0; i < 4; i++)
    rb_ary_push(ary, INT2FIX(r[i]));
  
  return ary;
}

/*
 * Fill a rectangle with the given Imlib2::Gradient at a given angle
 *
 * Examples:
 *   x, y, w, h = 10, 10, image.width - 20, image.height - 20
 *   angle = 45.2
 *   image.fill_gradient gradient, x, y, w, h, angle
 *
 *   rect = [5, 5, 500, 20]
 *   image.gradient gradient, rect, 36.8
 * 
 */
static VALUE image_fill_gradient(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  Imlib_Color_Range *grad;
  int x, y, w, h;
  double angle;
  
  switch (argc) {
    case 3:
      /* three arguments is a gradient, an array or hash of [x,y,w,h],
       * and an angle */
      switch (TYPE(argv[1])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[1], 0));
          y = NUM2INT(rb_ary_entry(argv[1], 1));
          w = NUM2INT(rb_ary_entry(argv[1], 2));
          h = NUM2INT(rb_ary_entry(argv[1], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      angle = NUM2DBL(argv[2]);
      break;
    case 4:
      switch (TYPE(argv[1])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[1], 0));
          y = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }

      switch (TYPE(argv[2])) {
        case T_HASH:
          w = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[2], rb_str_new2("h")));
          break;
        case T_ARRAY:
          w = NUM2INT(rb_ary_entry(argv[2], 0));
          h = NUM2INT(rb_ary_entry(argv[2], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      angle = NUM2DBL(argv[3]);
      break;
    case 6:
      x = NUM2INT(argv[1]);
      y = NUM2INT(argv[2]);
      w = NUM2INT(argv[3]);
      h = NUM2INT(argv[4]);
      angle = NUM2DBL(argv[5]);
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 3, 4, or 6)");
  }

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  Data_Get_Struct(argv[0], Imlib_Color_Range, grad);
  imlib_context_set_color_range(*grad);

  imlib_image_fill_color_range_rectangle(x, y, w, h, angle);

  return self;
}

/*
 * Draw an Imlib2::Polygon with the specified color
 *
 * Examples:
 *   # create a simple blue right triangle
 *   triangle = Imlib2::Polygon.new [10, 10], [20, 20], [10, 20]
 *   image.draw_polygon triangle, Imlib2::Color::BLUE
 *
 *   # create an open red square polygon
 *   square = Imlib2.Polygon.new [10, 10], [20, 10], [20, 20], [10, 20]
 *   image.draw_poly square, false, Imlib2::Color::RED
 *
 */
static VALUE image_draw_poly(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  ImlibPolygon *poly;
  VALUE color = Qnil;
  unsigned char closed = Qtrue;

  switch (argc) {
    case 1:
      /* one argument is poly.. closed is default (Qtrue) */
      break;
    case 2:
      /* two arguments is poly and closed, or poly and color */
      if ((rb_obj_is_kind_of(argv[1], cRgbaColor) == Qtrue) ||
          (rb_obj_is_kind_of(argv[1], cHsvaColor) == Qtrue) ||
          (rb_obj_is_kind_of(argv[1], cHlsaColor) == Qtrue) ||
          (rb_obj_is_kind_of(argv[1], cCmyaColor) == Qtrue)) {
        color = argv[1];
      } else /* FIXME: do type check here */ {
        closed = (argv[1] == Qtrue) ? 1 : 0;
      }
      break;
    case 3:
      /* two arguments is poly, closed, and color */
      closed = (argv[1] == Qtrue) ? 1 : 0;
      color = argv[2];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 3, 4, or 6)");
  }

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  if (color != Qnil)
    set_context_color(color);
  
  Data_Get_Struct(argv[0], ImlibPolygon, poly);
  imlib_image_draw_polygon(*poly, closed);

  return self;
}

/*
 * Fill an Imlib2::Polygon with the specified color
 *
 * Examples:
 *   # create an filled green diamond polygon
 *   square = Imlib2.Polygon.new [50, 10], [70, 30], [50, 50], [30, 30]
 *   image.fill_poly square, false, Imlib2::Color::GREEN
 *
 */
static VALUE image_fill_poly(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  ImlibPolygon *poly;
  VALUE color = Qnil;

  switch (argc) {
    case 1:
      /* one argument is poly.. closed is default (Qtrue) */
      break;
    case 2:
      color = argv[1];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 3, 4, or 6)");
  }

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  if (color != Qnil)
    set_context_color(color);
  
  Data_Get_Struct(argv[0], ImlibPolygon, poly);
  imlib_image_fill_polygon(*poly);

  return self;
}

/*
 * Apply an Imlib2::Filter (eg a static filter)
 *
 * You should probably using Imlib2::Image#filter() instead, since it is
 * polymorphic (eg, it can handle both static and scripted filters).
 * 
 * Example:
 *   filter = Imlib2::Filter.new 20
 *   filter.set 2, 2, Imlib2::Color::GREEN
 *   image.static_filter filter
 *
 */
static VALUE image_static_filter(VALUE self, VALUE filter) {
  ImStruct *im;
  Imlib_Filter *f;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  Data_Get_Struct(filter, Imlib_Filter, f);
  imlib_context_set_filter(*f);

  imlib_image_filter();

  return self;
}

/***************************/
/* SCRIPT FILTER FUNCTIONS */
/***************************/
/*
 * Apply a scripted filter
 *
 * You should probably using Imlib2::Image#filter() instead, since it is
 * polymorphic (eg, it can handle both static and scripted filters).
 * 
 * Example:
 *   x, y = 20, 10
 *   filter_string = "tint( x=#{x}, y=#{y}, red=255, alpha=55 );"
 *   image.script_filter filter_string
 *
 */
static VALUE image_script_filter(VALUE self, VALUE filter) {
  ImStruct *im;
  
  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  imlib_apply_filter(StringValuePtr(filter));

  return self;
}

/********************/
/* FILTER FUNCTIONS */
/********************/

/*
 * Apply a scripted filter or a static (eg Imlib2::Filter) filter 
 *
 * Example:
 *   # apply a static filter
 *   filter = Imlib2::Filter.new 20
 *   filter.set 2, 2, Imlib2::Color::GREEN
 *   image.filter filter
 *
 *   # apply a scripted filter
 *   x, y = 20, 10
 *   filter_string = "tint( x=#{x}, y=#{y}, red=255, alpha=55 );"
 *   image.filter filter_string
 *
 */
static VALUE image_filter(VALUE self, VALUE filter) {
  if (rb_obj_is_kind_of(self, rb_cString) == Qtrue) {
    return image_static_filter(self, filter);
  } else if (rb_obj_is_kind_of(self, cFilter) == Qtrue) {
    return image_script_filter(self, filter);
  } else {
    rb_raise(rb_eTypeError, "Invalid argument type "
                            "(not String or Imlib2::Filter)");
  }
  
  return self;
}

/*
 * Apply an Imlib2::ColorModifier to the image
 *
 * Examples:
 *   # modify the contrast of the entire image
 *   cmod = Imlib2::ColorModifier.new
 *   cmod.contrast = 1.5
 *   image.apply_cmod color_mod
 *
 *   # adjust the gamma of the given rect
 *   cmod = Imlib2::ColorModifier.new
 *   cmod.gamma = 0.5
 *   rect = [10, 10, 20, 40]
 *   image.apply_color_modifier cmod, rect
 *
 */
static VALUE image_apply_cmod(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  Imlib_Color_Modifier *cmod;
  char whole_image = 0;
  int x, y, w, h;

  x = y = w = h = 0;
  switch (argc) {
    case 1:
      whole_image = 1;
      break;
    case 2:
      switch (TYPE(argv[1])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));
          w = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("h")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[1], 0));
          y = NUM2INT(rb_ary_entry(argv[1], 1));
          w = NUM2INT(rb_ary_entry(argv[1], 2));
          h = NUM2INT(rb_ary_entry(argv[1], 3));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }
      break;
    case 5:
      x = NUM2INT(argv[1]);
      y = NUM2INT(argv[2]);
      w = NUM2INT(argv[3]);
      h = NUM2INT(argv[4]);
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 1, 2, or 5)");
  }

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  Data_Get_Struct(argv[0], Imlib_Color_Modifier, cmod);
  imlib_context_set_color_modifier(*cmod);

  if (whole_image)
    imlib_apply_color_modifier();
  else
    imlib_apply_color_modifier_to_rectangle(x, y, w, h);
  
  return self;
}

#ifndef X_DISPLAY_MISSING
/*
 * Render a pixmap and mask of an image.
 *
 * Note: this method returns an array with the pixmap and mask, not just
 * the pixmap.
 *
 * Examples:
 *   pmap, mask = image.render_pixmap
 *   pmap, mask = image.pixmap
 *
 *   # render a half-size pixmap and mask of the image
 *   pmap, mask = image.render_pixmap image.width / 2, image.height / 2
 *
 *   # render a half-size pixmap and mask of the image
 *   pmap, mask = image.pixmap image.width / 2, image.height / 2
 *
 */
static VALUE image_render_pixmap(int argc, VALUE *argv, VALUE self) {
  ImStruct *im;
  Imlib_Image old_im;
  Pixmap *pmap, *mask;
  VALUE ary;
  int w, h;
  char at_size = 0;

  switch (argc) {
    case 0:
      at_size = 0;
      break;
    case 1:
      /* one arg is an array or hash of [w, h] */

      at_size = 1;
      switch (TYPE(argv[0])) {
        case T_HASH:
          w = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("w")));
          h = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("h")));
          break;
        case T_ARRAY:
          w = NUM2INT(rb_ary_entry(argv[0], 0));
          h = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError,"Invalid argument type (not array or hash)");
      }

      break;
    case 2:
      /* two args is w, h */

      at_size = 1;
      w = NUM2INT(argv[0]);
      h = NUM2INT(argv[1]);
      break;
    default:
      rb_raise(rb_eArgError, "Invalid argument count (not 0, 1, or 2)");
  }
  
  /* save existing context */
  old_im = imlib_context_get_image();

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);

  /* if we were passed width and height, then use them */
  if (at_size)
    imlib_render_pixmaps_for_whole_image_at_size(pmap, mask, w, h);
  else
    imlib_render_pixmaps_for_whole_image(pmap, mask);

  /* restore existing context */
  imlib_context_set_image(old_im);
  
  ary = rb_ary_new();
  rb_ary_push(ary, Data_Wrap_Struct(cPixmap, 0, free, pmap));
  rb_ary_push(ary, Data_Wrap_Struct(cPixmap, 0, free, mask));

  return ary;
}
#endif /* !X_DISPLAY_MISSING */

/*
 * Attach an integer value to an Imlib2::Image.
 * 
 * Examples:
 *   image.attach_value('quality', 90)
 *   image['quality'] = 90
 */
static VALUE image_attach_val(VALUE self, VALUE key_o, VALUE val_o) {
  ImStruct *im;
  char *key;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  key = StringValuePtr(key_o);
  /* if (rb_obj_is_kind_of(val_o, rb_cString)) {
    void *val = (void*) StringValuePtr(val_o);
    fprintf(stderr, "attaching string\n");
    imlib_image_attach_data_value(key, val, 0, NULL);
  } else */ if (rb_obj_is_kind_of(val_o, rb_cNumeric)) {
    int val = NUM2INT(val_o);
    imlib_image_attach_data_value(key, NULL, val, NULL);
  } else {
    rb_raise(rb_eTypeError, "Invalid argument (not string or integer)");
  }

  /* return value */
  return val_o;
}

/*
 * Get an integer value attached to an Imlib2::Image.
 * 
 * Examples:
 *   qual = image.get_attached_value('quality')
 *   qual = image['quality']
 */
static VALUE image_get_attach_val(VALUE self, VALUE key_o) {
  ImStruct *im;
  VALUE ret;
  char *key;
  /* char *data; */

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  key = StringValuePtr(key_o);

  /* if it's got data, retrn that, otherwise return the value */
/* 
 *   if ((data = (char*) imlib_image_get_attached_value(key)) != NULL) {
 *     fprintf(stderr, "it's a string \"%s\": %x\n", key, data);
 *     ret = rb_str_new2(data);
 *   } else {
 *     fprintf(stderr, "it's an integer \"%s\"\n", key);
 */ 
    ret = INT2FIX(imlib_image_get_attached_value(key));
/* 
 *   }
 */ 
  
  /* return result */
  return ret;
}

/*
 * Remove an integer value attached to an Imlib2::Image.
 * 
 * Examples:
 *   image.remove_attached_value('quality')
 */
static VALUE image_rm_attach_val(VALUE self, VALUE key_o) {
  ImStruct *im;
  char *key;

  GET_AND_CHECK_IMAGE(self, im);
  imlib_context_set_image(im->im);
  key = StringValuePtr(key_o);

  imlib_image_remove_attached_data_value(key);

  return Qnil;
}

/******************/
/* CMOD FUNCTIONS */
/******************/
static void cmod_free(void *val) {
  Imlib_Color_Modifier *cmod = (Imlib_Color_Modifier*) val;

  imlib_context_set_color_modifier(*cmod);
  imlib_free_color_modifier();
  free(cmod);
}

/*
 * Returns a new Imlib2::ColorModifier
 *
 * Example:
 *   cmod = Imlib2::ColorModifier.new
 *
 */
VALUE cmod_new(VALUE klass) {
  Imlib_Color_Modifier *cmod;
  VALUE self;

  cmod = malloc(sizeof(Imlib_Color_Modifier));
  self = Data_Wrap_Struct(klass, 0, cmod_free, cmod);

  rb_obj_call_init(self, 0, NULL);

  return self;
}

/*
 * Imlib2::ColorModifier constructor
 *
 * This method takes no arguments.
 * 
 */
static VALUE cmod_init(VALUE self) {
  return self;
}

/*
 * Set the gamma value.
 *
 * Example:
 *   cmod.gamma = 0.5
 *
 */
static VALUE cmod_gamma(VALUE self, VALUE gamma) {
  Imlib_Color_Modifier *cmod;

  Data_Get_Struct(self, Imlib_Color_Modifier, cmod);
  imlib_context_set_color_modifier(*cmod);
  imlib_modify_color_modifier_gamma(NUM2DBL(gamma));

  return self;
}

/*
 * Set the brightness value.
 *
 * Example:
 *   cmod.brightness = 2.0
 *
 */
static VALUE cmod_brightness(VALUE self, VALUE brightness) {
  Imlib_Color_Modifier *cmod;

  Data_Get_Struct(self, Imlib_Color_Modifier, cmod);
  imlib_context_set_color_modifier(*cmod);
  imlib_modify_color_modifier_brightness(NUM2DBL(brightness));

  return self;
}

/*
 * Set the contrast value.
 *
 * Example:
 *   cmod.contrast = 0.8
 *
 */
static VALUE cmod_contrast(VALUE self, VALUE contrast) {
  Imlib_Color_Modifier *cmod;

  Data_Get_Struct(self, Imlib_Color_Modifier, cmod);
  imlib_context_set_color_modifier(*cmod);
  imlib_modify_color_modifier_contrast(NUM2DBL(contrast));

  return self;
}

/*
 * Reset the Imlib2::ColorModifier
 *
 * Example:
 *   cmod.reset
 *
 */
static VALUE cmod_reset(VALUE self) {
  Imlib_Color_Modifier *cmod;

  Data_Get_Struct(self, Imlib_Color_Modifier, cmod);
  imlib_context_set_color_modifier(*cmod);
  imlib_reset_color_modifier();

  return self;
}

/******************/
/* FONT FUNCTIONS */
/******************/
static void font_free(void *val) {
  Imlib_Font *font = (Imlib_Font*) val;
  imlib_context_set_font(*font);
  imlib_free_font();
  free(font);
}

/*
 * Returns a new Imlib2::Font
 *
 * Note: the specified font must be in the font path.  See
 * Imlib2::Font::list_paths() for a list of font paths, and
 * Imlib2::Font::list_fonts() for a list of fonts.
 *
 * Examples:
 *   font = Imlib2::Font.new 'helvetica/24'
 *   font = Imlib2::Font.load 'helvetica/24'
 *
 */
VALUE font_new(VALUE klass, VALUE font_name) {
  Imlib_Font *font;
  VALUE f_o;
  
  font = malloc(sizeof(Imlib_Font*));
  *font = imlib_load_font(StringValuePtr(font_name));

  f_o = Data_Wrap_Struct(klass, 0, font_free, font);
  rb_obj_call_init(f_o, 0, NULL);

  return f_o;
}

/* 
 * Constructor for Imlib2::Font
 *
 * Currently just a placeholder.
 *
 */
static VALUE font_init(VALUE self) {
  return self;
}

/*
 * Get the width and height of the given string using this font.
 *
 * Example:
 *   font = Imlib2::Font.new 'helvetica/12'
 *   size = font.size 'how big am i?'
 *   ['width', 'height'].each_index { |i, v|
 *     puts 'text ' << v << ' = ' << size[i]
 *   }
 *
 */
static VALUE font_text_size(VALUE self, VALUE text) {
  Imlib_Font *font;
  int   sw = 0, sh = 0;

  Data_Get_Struct(self, Imlib_Font, font);
  imlib_context_set_font(*font);
  imlib_get_text_size(StringValuePtr(text), &sw, &sh);
  
  return rb_ary_new3 (2, INT2FIX(sw), INT2FIX(sh));
}
  
/* 
 * Get the horizontal and vertical advance of the given string using
 * this font.
 *
 * Example:
 *   font = Imlib2::Font.new 'verdana/36'
 *   advances = font.advance "what's my advance?"
 *   ['horizontal', 'vertical'].each_index { |i, v|
 *     puts 'text ' << v << ' advance = ' << advances[i]
 *   }
 *
 */
static VALUE font_text_advance(VALUE self, VALUE text) {
  Imlib_Font *font;
  int   sw = 0, sh = 0;

  Data_Get_Struct(self, Imlib_Font, font);
  imlib_context_set_font(*font);
  imlib_get_text_advance(StringValuePtr(text), &sw, &sh);
  
  return rb_ary_new3 (2, INT2FIX(sw), INT2FIX (sh));
}
  
/*
 * Get the inset of the given string using this font
 *
 * Example:
 *   font = Imlib2::Font.new 'palatino/9'
 *   inset = font.inset 'wonder what the inset for this string is...'
 *
 */
static VALUE font_text_inset(VALUE self, VALUE text) {
  Imlib_Font *font;

  Data_Get_Struct(self, Imlib_Font, font);
  imlib_context_set_font(*font);
  
  return INT2FIX(imlib_get_text_inset(StringValuePtr(text)));
}
  
/*
 * Get the character index of the pixel at the given coordinates using
 * this font.
 *
 * Example:
 *   x, y, char_w, char_h = font.index "index\nstring\n", 5, 5
 *
 */
static VALUE font_text_index(int argc, VALUE *argv, VALUE self) {
  Imlib_Font *font;
  VALUE text, ary;
  int x, y, i, r[] = { 0, 0, 0, 0 };

  text = argv[0];
  switch (argc) {
    case 2:
      /* two arguments is a string, and an array or hash of x, y */
      switch (TYPE(argv[1])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[1], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[1], 0));
          y = NUM2INT(rb_ary_entry(argv[1], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
      }
      break;
    case 3:
      /* three arguments is a string, x, y */
      x = NUM2INT(argv[1]);
      y = NUM2INT(argv[2]);
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
  }

  Data_Get_Struct(self, Imlib_Font, font);
  imlib_context_set_font(*font);
  imlib_text_get_index_and_location(StringValuePtr(text), x, y,
                                    &r[0], &r[1], &r[2], &r[3]);
  ary = rb_ary_new();
  for (i = 0; i < 4; i++)
    rb_ary_push(ary, INT2FIX(r[i]));
  
  return ary;
}
  
/*
 * Get the character coordinates of the at the given index using this font.
 *
 * Example:
 *   x, y, char_w, char_h = font.index "index\nstring\n", 8
 *
 */
static VALUE font_text_location(VALUE self, VALUE text, VALUE index) {
  Imlib_Font *font;
  VALUE ary;
  int i, r[] = { 0, 0, 0, 0 };

  Data_Get_Struct(self, Imlib_Font, font);
  imlib_context_set_font(*font);
  imlib_text_get_location_at_index(StringValuePtr(text), NUM2INT(index), 
                                   &r[0], &r[1], &r[2], &r[3]);

  ary = rb_ary_new();
  for (i = 0; i < 4; i++)
    rb_ary_push(ary, INT2FIX(r[i]));
  
  return ary;
}
  
/*
 * Get font ascent.
 *
 * Example:
 *   a = font.ascent
 *
 */
static VALUE font_ascent(VALUE self) {
  Imlib_Font *font;

  Data_Get_Struct(self, Imlib_Font, font);
  imlib_context_set_font(*font);

  return INT2FIX(imlib_get_font_ascent());
}

/*
 * Get font descent.
 *
 * Example:
 *   a = font.descent
 *
 */
static VALUE font_descent(VALUE self) {
  Imlib_Font *font;

  Data_Get_Struct(self, Imlib_Font, font);
  imlib_context_set_font(*font);

  return INT2FIX(imlib_get_font_descent());
}
  
/*
 * Get font maximum ascent.
 *
 * Example:
 *   a = font.maximum_ascent
 *
 */
static VALUE font_maximum_ascent(VALUE self) {
  Imlib_Font *font;

  Data_Get_Struct(self, Imlib_Font, font);
  imlib_context_set_font(*font);

  return INT2FIX(imlib_get_maximum_font_ascent());
}

/*
 * Get font maximum descent.
 *
 * Example:
 *   a = font.maximum_descent
 *
 */
static VALUE font_maximum_descent(VALUE self) {
  Imlib_Font *font;

  Data_Get_Struct(self, Imlib_Font, font);
  imlib_context_set_font(*font);

  return INT2FIX(imlib_get_maximum_font_descent());
}

/*
 * Return an array of all known fonts
 *
 * Example:
 *   font_list = Imlib2::Font.list_fonts
 *
 */
static VALUE font_list_fonts(VALUE klass) {
  VALUE ary;
  char **list;
  int i, len;
  UNUSED(klass);

  list = imlib_list_fonts(&len);

  ary = rb_ary_new();
  for (i = 0; i < len; i++)
    rb_ary_push(ary, rb_str_new2(list[i]));

  /* FIXME: there has got to be a better way to do this: */
  imlib_free_font_list(list, len);
  
  return ary;
}

/*
 * Add a path to the list of font paths.
 *
 * Example:
 *   Imlib2::Font.add_path '/usr/lib/X11/fonts/Truetype'
 *
 */
static VALUE font_add_path(VALUE klass, VALUE path) {
  UNUSED(klass);
  imlib_add_path_to_font_path(StringValuePtr(path));
  return Qtrue;
}

/*
 * Remove a path from the list of font paths.
 *
 * Example:
 *   Imlib2::Font.remove_path '/usr/lib/X11/fonts/Truetype'
 *
 */
static VALUE font_remove_path(VALUE klass, VALUE path) {
  UNUSED(klass);
  imlib_remove_path_from_font_path(StringValuePtr(path));
  return Qtrue;
}

/*
 * Return an array of font paths.
 *
 * Example:
 *   path_list = Imlib2::Font.list_paths
 *
 */
static VALUE font_list_paths(VALUE klass) {
  VALUE ary;
  char **list;
  int i, len;
  UNUSED(klass);

  list = imlib_list_font_path(&len);

  ary = rb_ary_new();
  for (i = 0; i < len; i++)
    rb_ary_push(ary, rb_str_new2(list[i]));

  /* FIXME: there has got to be a better way to do this: */
  imlib_free_font_list(list, len);
  
  return ary;
}

/**********************/
/* GRADIENT FUNCTIONS */
/**********************/
static void gradient_free(void *val) {
  Imlib_Color_Range *range = (Imlib_Color_Range*) val;
  imlib_context_set_color_range(*range);
  imlib_free_color_range();
  free(range);
}

/*
 * Return a new Imlib2::Gradient.
 *
 * Examples:
 *   # create a blue to green gradient
 *   grad = Imlib2::Gradient.new
 *   grad.add_color 0, Imlib2::Color::BLUE
 *   grad.add_color 100, Imlib2::Color::GREEN
 *   
 *   # create a red to yellow to black gradient
 *   colors = [ [0,   Imlib2::Color::RED    ], 
 *              [100, Imlib2::Color::YELLOW ],
 *              [200, Imlib2::Color::BLACK  ] ]
 *   grad = Imlib2::Gradient.new *colors
 *
 */
VALUE gradient_new(int argc, VALUE *argv, VALUE klass) {
  Imlib_Color_Range *range;
  VALUE g_o;
  
  range = malloc(sizeof(Imlib_Color_Range*));
  *range = imlib_create_color_range();

  g_o = Data_Wrap_Struct(klass, 0, gradient_free, range);
  rb_obj_call_init(g_o, argc, argv);

  return g_o;
}

/*
 * Add a color at the given distance
 *
 * Examples:
 *   # add the context color with a 100 pixel offset
 *   grad.add_color 100
 *
 *   # add white at a 100 pixel offset
 *   grad.add_color 100, Imlib2::Color::WHITE
 *
 */
static VALUE gradient_add_color(int argc, VALUE *argv, VALUE self) {
  Imlib_Color_Range *grad;
  VALUE color = Qnil;
  int distance;

  switch (argc) {
    case 1:
      distance = NUM2INT(argv[0]);
      break;
    case 2:
      distance = NUM2INT(argv[0]);
      color = argv[1];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 1 or 2)");
  }

  Data_Get_Struct(self, Imlib_Color_Range, grad);
  imlib_context_set_color_range(*grad);

  if (color != Qnil)
    set_context_color(color);

  imlib_add_color_to_color_range(distance);

  return self;
}

/*
 * Imlib2::Gradient constructor.
 *
 * Accepts an arbitrary number of arrays of offset, and color values
 * (the same as Imlib2::Gradient.new).
 *
 */
static VALUE gradient_init(int argc, VALUE *argv, VALUE self) {
  int i;

  for (i = 0; i < argc; i++) {
    VALUE args[2];
    args[0]= rb_ary_entry(argv[i], 0);
    args[1] = rb_ary_entry(argv[i], 1);

    gradient_add_color(2, args, self);
  }
  
  return self;
}

/*********************/
/* POLYGON FUNCTIONS */
/*********************/
static void poly_free(void *val) {
  ImlibPolygon *poly = (ImlibPolygon*) val;
  imlib_polygon_free(*poly);
  free(poly);
}

/* 
 * Return a new Imlib2::Polygon
 *
 * Examples:
 *   poly = Imlib2::Polygon.new
 *
 *   points = [[10, 10], [20, 30], [15, 8], [6, 3], [12, 2]]
 *   poly = Imlib2::Polygon.new *points
 *
 */
VALUE poly_new(int argc, VALUE *argv, VALUE klass) {
  ImlibPolygon *poly;
  VALUE p_o;

  poly = malloc(sizeof(ImlibPolygon*));
  *poly = imlib_polygon_new();

  p_o = Data_Wrap_Struct(klass, 0, poly_free, poly);
  rb_obj_call_init(p_o, argc, argv);

  return p_o;
}

/*
 * Add a point to the polygon
 *
 * Example:
 *   poly.add_point 123, 456
 *
 */
static VALUE poly_add_point(int argc, VALUE *argv, VALUE self) {
  ImlibPolygon *poly;
  int x, y;

  switch (argc) {
    case 1:
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
      }
      break;
    case 2:
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
  }
  
  Data_Get_Struct(self, ImlibPolygon, poly);
  imlib_polygon_add_point(*poly, x, y);
    
  return self;
}

/*
 * Constructor for Imlib2::Polygon
 *
 * Accepts the same arguments as Imlib2::Polygon.new.
 *
 */
static VALUE poly_init(int argc, VALUE *argv, VALUE self) {
  VALUE args[1];
  int i;

  for (i = 0; i < argc; i++) {
    args[0] = argv[i];
    poly_add_point(1, args, self);
  }

  return self;
}

/*
 * Return the bounding rectangle of the given polygon.
 *
 * Example:
 *   bounds = poly.bounds
 *   %q(x y w h).each_index { |i, v| puts v << ' = ' << bounds[i] }
 *
 */
static VALUE poly_bounds(VALUE self) {
  ImlibPolygon *poly;
  VALUE ary;
  int i, r[4] = { 0, 0, 0, 0 };

  Data_Get_Struct(self, ImlibPolygon, poly);
  imlib_polygon_get_bounds(*poly, &r[0], &r[1], &r[2], &r[3]);

  ary = rb_ary_new();
  for (i = 0; i < 4; i++)
    rb_ary_push(ary, INT2FIX(r[i]));

  return ary;
}
  
/*
 * Does the given point lie within the polygon?
 *
 * Example:
 *   if poly.contains? 12, 5
 *     puts 'yes'
 *   else
 *     puts 'no'
 *   end
 *
 */
static VALUE poly_contains(int argc, char *argv, VALUE self) {
  ImlibPolygon *poly;
  int x, y;

  switch (argc) {
    case 1:
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 2:
      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
  }
  
  Data_Get_Struct(self, ImlibPolygon, poly);
  return imlib_polygon_contains_point(*poly, x, y) ? Qtrue : Qfalse;
}

/***************************/
/* STATIC FILTER FUNCTIONS */
/***************************/
static void filter_free(void *filter) {
  Imlib_Filter *f = (Imlib_Filter*) filter;
  imlib_context_set_filter(*f);
  imlib_free_filter();
  free(f);
}

/*
 * Return a new Imlib2::Filter
 *
 * Example:
 *   filter = Imlib2::Filter.new
 *
 */
VALUE filter_new(VALUE initsize, VALUE klass) {
  Imlib_Filter *f = malloc(sizeof(Imlib_Filter));
  VALUE f_o, vals[1];

  *f = imlib_create_filter(NUM2INT(initsize));
  f_o = Data_Wrap_Struct(klass, 0, filter_free, f);

  vals[0] = initsize;
  rb_obj_call_init(f_o, 1, vals);

  return f_o;
}

/*
 * Constructor for Imlib2::Filter
 *
 * Currently just a placeholder.
 *
 */
VALUE filter_init(VALUE self, VALUE initsize) {
  UNUSED(initsize);
  /* don't do anythign here for now */
  return self;
}

/*
 * Set the filter color value and x, y
 * 
 * Example: 
 *   filter.set x, y, Imlib2::Color::RED
 *
 */
VALUE filter_set(int argc, VALUE *argv, VALUE self) {
  Imlib_Filter *f;
  Imlib_Color *c;
  int x, y;
  VALUE color;

  switch (argc) {
    case 2:
      /* 2 args is an array or hash of x, y, and an RGBAColor */

      color = argv[1];
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three args is x, y, and color */

      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      color = argv[2];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
  }

  Data_Get_Struct(self, Imlib_Filter, f);
  Data_Get_Struct(color, Imlib_Color, c);
  imlib_context_set_filter(*f);
  imlib_filter_set(x, y, c->alpha, c->red, c->green, c->blue);

  return self;
}

VALUE filter_set_red(int argc, VALUE *argv, VALUE self) {
  Imlib_Filter *f;
  Imlib_Color *c;
  int x, y;
  VALUE color;

  switch (argc) {
    case 2:
      /* 2 args is an array or hash of x, y, and an RGBAColor */

      color = argv[1];
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three args is x, y, and color */

      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      color = argv[2];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
  }

  Data_Get_Struct(self, Imlib_Filter, f);
  Data_Get_Struct(color, Imlib_Color, c);
  imlib_context_set_filter(*f);
  imlib_filter_set_red(x, y, c->alpha, c->red, c->green, c->blue);

  return self;
}

/*
 * Set the green filter color value and x, y
 * 
 * Example: 
 *   filter.set_green x, y, Imlib2::Color::RgbaColor.new 0, 128, 0, 128
 *
 */
VALUE filter_set_green(int argc, VALUE *argv, VALUE self) {
  Imlib_Filter *f;
  Imlib_Color *c;
  int x, y;
  VALUE color;

  switch (argc) {
    case 2:
      /* 2 args is an array or hash of x, y, and an RGBAColor */

      color = argv[1];
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three args is x, y, and color */

      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      color = argv[2];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
  }

  Data_Get_Struct(self, Imlib_Filter, f);
  Data_Get_Struct(color, Imlib_Color, c);
  imlib_context_set_filter(*f);
  imlib_filter_set_green(x, y, c->alpha, c->red, c->green, c->blue);

  return self;
}

/*
 * Set the blue filter color value and x, y
 * 
 * Example: 
 *   filter.set_blue x, y, Imlib2::Color::RgbaColor.new 0, 0, 64, 128
 *
 */
VALUE filter_set_blue(int argc, VALUE *argv, VALUE self) {
  Imlib_Filter *f;
  Imlib_Color *c;
  int x, y;
  VALUE color;

  switch (argc) {
    case 2:
      /* 2 args is an array or hash of x, y, and an RGBAColor */

      color = argv[1];
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three args is x, y, and color */

      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      color = argv[2];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
  }

  Data_Get_Struct(self, Imlib_Filter, f);
  Data_Get_Struct(color, Imlib_Color, c);
  imlib_context_set_filter(*f);
  imlib_filter_set_blue(x, y, c->alpha, c->red, c->green, c->blue);

  return self;
}

/*
 * Set the alpha filter color value and x, y
 * 
 * Example: 
 *   filter.set_alpha x, y, Imlib2::Color::RgbaColor.new 0, 0, 0, 128
 *
 */
VALUE filter_set_alpha(int argc, VALUE *argv, VALUE self) {
  Imlib_Filter *f;
  Imlib_Color *c;
  int x, y;
  VALUE color;

  switch (argc) {
    case 2:
      /* 2 args is an array or hash of x, y, and an RGBAColor */

      color = argv[1];
      switch (TYPE(argv[0])) {
        case T_HASH:
          x = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("x")));
          y = NUM2INT(rb_hash_aref(argv[0], rb_str_new2("y")));
          break;
        case T_ARRAY:
          x = NUM2INT(rb_ary_entry(argv[0], 0));
          y = NUM2INT(rb_ary_entry(argv[0], 1));
          break;
        default:
          rb_raise(rb_eTypeError, "Invalid argument type (not array or hash)");
      }
      break;
    case 3:
      /* three args is x, y, and color */

      x = NUM2INT(argv[0]);
      y = NUM2INT(argv[1]);
      color = argv[2];
      break;
    default:
      rb_raise(rb_eTypeError, "Invalid argument count (not 2 or 3)");
  }

  Data_Get_Struct(self, Imlib_Filter, f);
  Data_Get_Struct(color, Imlib_Color, c);
  imlib_context_set_filter(*f);
  imlib_filter_set_alpha(x, y, c->alpha, c->red, c->green, c->blue);

  return self;
}

/*
 * Set filter constants.
 *
 * Example:
 *   filter.set_constants RgbaColor.new 32, 32, 32, 32
 *
 */
VALUE filter_constants(VALUE self, VALUE color) {
  Imlib_Filter *f;
  Imlib_Color *c;

  Data_Get_Struct(self, Imlib_Filter, f);
  Data_Get_Struct(color, Imlib_Color, c);
  imlib_context_set_filter(*f);
  imlib_filter_constants(c->alpha, c->red, c->green, c->blue);

  return self;
}

/*
 * Set filter divisors.
 *
 * Example:
 *   filter.set_divisors RgbaColor.new 0, 32, 0, 32
 *
 */
VALUE filter_divisors(VALUE self, VALUE color) {
  Imlib_Filter *f;
  Imlib_Color *c;

  Data_Get_Struct(self, Imlib_Filter, f);
  Data_Get_Struct(color, Imlib_Color, c);
  imlib_context_set_filter(*f);
  imlib_filter_divisors(c->alpha, c->red, c->green, c->blue);

  return self;
}

/*********************/
/* CONTEXT FUNCTIONS */
/*********************/
static void ctx_free(void *val) {
  Imlib_Context *ctx = (Imlib_Context *) val;

  imlib_context_free(*ctx);
  free(ctx);
}

/*
 * Return a new Imlib2::Context.
 *
 * Example:
 *   ctx = Imlib2::Context.new
 *
 */
VALUE ctx_new(VALUE klass) {
  VALUE self;
  Imlib_Context *ctx;

  ctx = malloc(sizeof(Imlib_Context));
  *ctx = imlib_context_new();

  self = Data_Wrap_Struct(klass, 0, ctx_free, ctx);
  rb_obj_call_init(self, 0, NULL);
  
  return self;
}

/*
 * Imlib2::Context constructor.
 *
 * Currently just a placeholder
 *
 */
static VALUE ctx_init(VALUE self) {
  return self;
}

/*
 * Pop the top context off the context stack.
 *
 * Example:
 *   ctx = Imlib2::Context.pop
 *
 */
VALUE ctx_pop(VALUE klass) {
  Imlib_Context *ctx;

  ctx = (Imlib_Context *) malloc(sizeof(Imlib_Context));
  imlib_context_pop();
  *ctx = imlib_context_get();

  return Data_Wrap_Struct(klass, 0, ctx_free, ctx);
}

/*
 * Return the current context.
 *
 * Example:
 *   ctx = Imlib2::Context.get
 *
 */
VALUE ctx_get(VALUE klass) {
  Imlib_Context *ctx;

  ctx = (Imlib_Context *) malloc(sizeof(Imlib_Context));
  *ctx = imlib_context_get();

  return Data_Wrap_Struct(klass, 0, ctx_free, ctx);
}

/*
 * Push this context onto the context stack.
 *
 * Example:
 *   ctx.push
 *
 */
static VALUE ctx_push(VALUE self) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);

  return self;
}

/*
 * Set the dither_mask flag.
 *
 * Example:
 *   ctx.dither_mask = true
 *
 */
static VALUE ctx_set_dither_mask(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_set_dither_mask(val != Qnil && val != Qfalse);
  imlib_context_pop();

  return self;
}

/*
 * Get the dither_mask flag.
 *
 * Example:
 *   if ctx.dither_mask == true
 *     puts 'dither_mask enabled'
 *   end
 *
 */
static VALUE ctx_dither_mask(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qfalse;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = imlib_context_get_dither_mask() ? Qtrue : Qfalse;
  imlib_context_pop();

  return r;
}

/*
 * Set the anti_alias flag.
 *
 * Example:
 *   ctx.anti_alias = true
 *
 */
static VALUE ctx_set_aa(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_set_anti_alias(val != Qnil && val != Qfalse);
  imlib_context_pop();

  return self;
}

/*
 * Get the anti_alias flag.
 *
 * Example:
 *   if ctx.anti_alias == true
 *     puts 'anti_alias enabled.'
 *   end
 *
 */
static VALUE ctx_aa(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qfalse;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = imlib_context_get_anti_alias() ? Qtrue : Qfalse;
  imlib_context_pop();

  return r;
}

/*
 * Set the dither flag.
 *
 * Example:
 *   ctx.dither = true
 *
 */
static VALUE ctx_set_dither(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_set_dither(val != Qnil && val != Qfalse);
  imlib_context_pop();

  return self;
}

/*
 * Get the dither flag.
 *
 * Example:
 *   if ctx.dither
 *     puts 'dither enabled.'
 *   end
 *
 */
static VALUE ctx_dither(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qfalse;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = imlib_context_get_dither() ? Qtrue : Qfalse;
  imlib_context_pop();

  return r;
}

/*
 * Set the blend flag.
 *
 * Example:
 *   ctx.blend = true
 *
 */
static VALUE ctx_set_blend(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_set_blend(val != Qnil && val != Qfalse);
  imlib_context_pop();

  return self;
}

/*
 * Get the blend flag.
 *
 * Example:
 *   if ctx.blend
 *     puts 'blend enabled.'
 *   end
 *
 */
static VALUE ctx_blend(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qfalse;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = imlib_context_get_blend() ? Qtrue : Qfalse;
  imlib_context_pop();

  return r;
}

/*
 * Set the current color modifier (Imlib2::ColorModifier).
 *
 * Example:
 *   ctx.cmod = cmod
 *
 */
static VALUE ctx_set_cmod(VALUE self, VALUE val) {
  Imlib_Context *ctx;
  Imlib_Color_Modifier *cmod;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  Data_Get_Struct(val, Imlib_Color_Modifier, cmod);
  imlib_context_set_color_modifier(*cmod);
  imlib_context_pop();

  return self;
}

/*
 * Get the current color modifier (Imlib2::ColorModifier).
 *
 * Example:
 *   cmod = ctx.cmod
 *
 */
static VALUE ctx_cmod(VALUE self) {
  Imlib_Context *ctx;
  Imlib_Color_Modifier *cmod;

  cmod = malloc(sizeof(Imlib_Color_Modifier));
  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  *cmod = imlib_context_get_color_modifier();
  imlib_context_pop();

  return Data_Wrap_Struct(cColorMod, 0, cmod_free, cmod);
}

/*
 * Set the current operation (Imlib2::Op or Imlib2::Operation).
 *
 * Example:
 *   ctx.operation = Imlib2::Op::COPY
 *
 */
static VALUE ctx_set_op(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_set_operation(NUM2INT(val));
  imlib_context_pop();

  return self;
}

/*
 * Get the current operation (Imlib2::Op or Imlib2::Operation).
 *
 * Example:
 *   if ctx.op == Imlib2::Op::COPY
 *     puts 'copy operation'
 *   end
 *
 */
static VALUE ctx_op(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qnil;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = INT2FIX(imlib_context_get_operation());
  imlib_context_pop();

  return r;
}

/*
 * Set the current font (Imlib2::Font).
 *
 * Example:
 *   ctx.font = Imlib2::Font.new 'helvetica/12'
 *
 */
static VALUE ctx_set_font(VALUE self, VALUE val) {
  Imlib_Context *ctx;
  Imlib_Font *font;

  font = malloc(sizeof(Imlib_Font));
  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  Data_Get_Struct(val, Imlib_Font, font);
  imlib_context_set_font(*font);
  imlib_context_pop();

  return self;
}

/*
 * Get the current font (Imlib2::Font).
 *
 * Example:
 *   font = ctx.font
 *
 */
static VALUE ctx_font(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qnil;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = Data_Wrap_Struct(cFont, 0, font_free, imlib_context_get_font());
  imlib_context_pop();

  return r;
}

/*
 * Set the current font direction (Imlib2::Dir or Imlib2::Direction).
 *
 * Example:
 *   ctx.direction = Imlib2::Direction::LEFT
 *
 */
static VALUE ctx_set_dir(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_set_direction(NUM2INT(val));
  imlib_context_pop();

  return self;
}

/*
 * Get the current font direction (Imlib2::Dir or Imlib2::Direction).
 *
 * Example:
 *   if ctx.direction != Imlib2::Direction::RIGHT
 *     puts 'drawing funny text'
 *   end
 *
 */
static VALUE ctx_dir(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qnil;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = INT2FIX(imlib_context_get_direction());
  imlib_context_pop();

  return r;
}

/*
 * Set the text drawing angle.
 *
 * Example:
 *   ctx.angle = 76.8
 *
 */
static VALUE ctx_set_angle(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_set_angle(NUM2DBL(val));
  imlib_context_pop();

  return self;
}

/*
 * Get the text drawing angle.
 *
 * Example:
 *   if ctx.dir == Imlib2::Direction::ANGLE
 *     puts 'the current font angle is ' << ctx.angle
 *   end
 *
 */
static VALUE ctx_angle(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qnil;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = rb_float_new(imlib_context_get_angle());
  imlib_context_pop();

  return r;
}

/*
 * Set the current color (Imlib2::Color).
 *
 * Example:
 *   ctx.color = Imlib2::Color::LIGHTGRAY
 *
 */
static VALUE ctx_set_color(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  set_context_color(val);
  imlib_context_pop();

  return self;
}

/*
 * Get the current color (Imlib2::Color::RgbaColor).
 *
 * Example:
 *   color = ctx.color
 *
 */
static VALUE ctx_color(VALUE self) {
  Imlib_Context *ctx;
  VALUE argv[4];
  int i, r[4];

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_get_color(&(r[0]), &(r[1]), &(r[2]), &(r[3]));
  imlib_context_pop();

  for (i = 0; i < 4; i++) 
    argv[i] = INT2NUM(r[i]);

  return rgba_color_new(4, argv, cRgbaColor);
}

/*
 * Set the current gradient (Imlib2::Gradient).
 *
 * Example:
 *   ctx.gradient = grad
 *
 */
static VALUE ctx_set_gradient(VALUE self, VALUE val) {
  Imlib_Context *ctx;
  Imlib_Color_Range *gradient;

  gradient = malloc(sizeof(Imlib_Color_Range));
  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  Data_Get_Struct(val, Imlib_Color_Range, gradient);
  imlib_context_set_color_range(*gradient);
  imlib_context_pop();

  return self;
}

/*
 * Get the current gradient (Imlib2::Gradient).
 *
 * Example:
 *   grad = ctx.gradient
 *
 */
static VALUE ctx_gradient(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qnil;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = Data_Wrap_Struct(cGradient, 0, gradient_free, imlib_context_get_color_range());
  imlib_context_pop();

  return r;
}

/*
 * Set the progress callback granularity.
 *
 * This function is not useful at the moment since you cannot specify
 * progress callbacks from within ruby (this is a TODO item).
 *
 * Example:
 *   ctx.progress_granularity = 10
 *
 */
static VALUE ctx_set_progress_granularity(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_set_progress_granularity(NUM2INT(val));
  imlib_context_pop();

  return self;
}

/*
 * Get the progress callback granularity.
 *
 * This function is not useful at the moment since you cannot specify
 * progress callbacks from within Ruby (this is a TODO item).
 *
 * Example:
 *   granularity = ctx.progress_granularity
 *
 */
static VALUE ctx_progress_granularity(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qnil;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = INT2FIX(imlib_context_get_progress_granularity());
  imlib_context_pop();

  return r;
}

/*
 * Set the current image (Imlib2::Image).
 *
 * Note that this function is not useful at the moment since all image
 * instance methods blindly blow away the image and color context.  So
 * you cannot safely mix image context and image instance methods.
 *
 * Example:
 *   ctx.image = image
 *
 */
static VALUE ctx_set_image(VALUE self, VALUE val) {
  Imlib_Context *ctx;
  ImStruct *im;

  im = malloc(sizeof(ImStruct));
  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  GET_AND_CHECK_IMAGE(val, im);
  imlib_context_set_image(im->im);
  imlib_context_pop();

  return self;
}

/*
 * Get the current image (Imlib2::Image).
 *
 * Note that this function is not useful at the moment since all image
 * instance methods blindly blow away the image and color context.  So
 * you cannot safely mix image context and image instance methods.
 *
 * Example:
 *   im = ctx.image
 *
 */
static VALUE ctx_image(VALUE self) {
  Imlib_Context *ctx;
  ImStruct *im;
  VALUE r = Qnil;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  im = malloc(sizeof(ImStruct));
  im->im = imlib_context_get_image();
  r = Data_Wrap_Struct(cImage, 0, im_struct_free, im);
  imlib_context_pop();

  return r;
}

/*
 * Set the cliprect.
 *
 * Example:
 *   ctx.cliprect = [10, 10, 100, 100]
 *
 */
static VALUE ctx_set_cliprect(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_set_cliprect(
    NUM2INT(rb_ary_entry(val, 0)), 
    NUM2INT(rb_ary_entry(val, 1)), 
    NUM2INT(rb_ary_entry(val, 2)), 
    NUM2INT(rb_ary_entry(val, 3))
  );
  imlib_context_pop();

  return self;
}

/*
 * Get the cliprect.
 *
 * Example:
 *   x, y, w, h = ctx.cliprect
 *
 */
static VALUE ctx_cliprect(VALUE self) {
  Imlib_Context *ctx;
  int i, r[4];
  VALUE ary;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_get_cliprect(&(r[0]), &(r[1]), &(r[2]), &(r[3]));
  imlib_context_pop();

  ary = rb_ary_new();
  for (i = 0; i < 4; i++);
    rb_ary_push(ary, NUM2INT(r[i]));

  return ary;
}

/*
 * Set the current TrueType Font Encoding.
 *
 * Example:
 *   ctx.encoding = Imlib2::Encoding::ISO_8859_5
 *
 */
static VALUE ctx_set_encoding(VALUE self, VALUE val) {
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  imlib_context_set_TTF_encoding(NUM2INT(val));
  imlib_context_pop();

  return self;
}

/*
 * Get the current TrueType Font Encoding.
 *
 * Example:
 *   if ctx.encoding == Imlib2::Encoding::ISO_8859_1
 *     puts 'using ISO-8859-1 encoding'
 *   end
 *
 */
static VALUE ctx_encoding(VALUE self) {
  Imlib_Context *ctx;
  VALUE r = Qnil;

  Data_Get_Struct(self, Imlib_Context, ctx);
  imlib_context_push(*ctx);
  r = INT2FIX(imlib_context_get_TTF_encoding());
  imlib_context_pop();

  return r;
}

#ifndef X_DISPLAY_MISSING
/*************************/
/* CONTEXT X11 FUNCTIONS */
/*************************/
/*
 * Set the current X11 Display.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   context.set_display display
 *   context.display = display
 *
 */
static VALUE ctx_set_display(VALUE self, VALUE display) {
  Display *disp;
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  Data_Get_Struct(display, Display, disp);

  imlib_context_push(*ctx);
  imlib_context_set_display(disp);
  imlib_context_pop();

  return display;
}

/*
 * Get the current X11 Display.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   display = context.get_display
 *   display = context.display
 *
 */
static VALUE ctx_display(VALUE self) {
  Imlib_Context *ctx;
  VALUE disp;

  Data_Get_Struct(self, Imlib_Context, ctx);

  imlib_context_push(*ctx);
  disp = Data_Wrap_Struct(cDisplay, NULL, XFree, imlib_context_get_display());
  imlib_context_pop();

  return disp;
}

/*
 * Set the current X11 Visual.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   context.set_visual visual
 *   context.visual = visual
 *
 */
static VALUE ctx_set_visual(VALUE self, VALUE visual) {
  Visual *vis;
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  Data_Get_Struct(visual, Visual, vis);

  imlib_context_push(*ctx);
  imlib_context_set_visual(vis);
  imlib_context_pop();

  return visual;
}

/*
 * Get the current X11 Visual.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   visual = context.get_visual
 *   visual = context.visual
 *
 */
static VALUE ctx_visual(VALUE self) {
  Imlib_Context *ctx;
  VALUE vis;

  Data_Get_Struct(self, Imlib_Context, ctx);

  imlib_context_push(*ctx);
  vis = Data_Wrap_Struct(cVisual, NULL, XFree, imlib_context_get_visual());
  imlib_context_pop();

  return vis;
}

/*
 * Set the current X11 Colormap.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   context.set_colormap colormap
 *   context.colormap = colormap
 *
 */
static VALUE ctx_set_colormap(VALUE self, VALUE colormap) {
  Colormap *cmap;
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  Data_Get_Struct(colormap, Colormap, cmap);

  imlib_context_push(*ctx);
  imlib_context_set_colormap(*cmap);
  imlib_context_pop();

  return colormap;
}

/*
 * Get the current X11 Colormap.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   colormap = context.get_colormap
 *   colormap = context.colormap
 *
 */
static VALUE ctx_colormap(VALUE self) {
  Imlib_Context *ctx;
  Colormap *cmap;
  VALUE colormap;

  Data_Get_Struct(self, Imlib_Context, ctx);
  cmap = malloc(sizeof(Colormap));

  imlib_context_push(*ctx);
  *cmap = imlib_context_get_colormap();
  colormap = Data_Wrap_Struct(cColormap, 0, dont_free, cmap);
  imlib_context_pop();

  return colormap;
}

/*
 * Set the current X11 Drawable.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   context.set_drawable drawable
 *   context.drawable = drawable
 *
 */
static VALUE ctx_set_drawable(VALUE self, VALUE drawable) {
  Drawable *draw;
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  Data_Get_Struct(drawable, Drawable, draw);

  imlib_context_push(*ctx);
  imlib_context_set_drawable(*draw);
  imlib_context_pop();

  return drawable;
}

/*
 * Get the current X11 Drawable.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   drawable = context.get_drawable
 *   drawable = context.drawable
 *
 */
static VALUE ctx_drawable(VALUE self) {
  Imlib_Context *ctx;
  Drawable *draw;
  VALUE drawable;

  Data_Get_Struct(self, Imlib_Context, ctx);
  draw = malloc(sizeof(Drawable));

  imlib_context_push(*ctx);
  *draw = imlib_context_get_drawable();
  drawable = Data_Wrap_Struct(cDrawable, NULL, dont_free, draw);
  imlib_context_pop();

  return drawable;
}

/*
 * Set the current X11 Mask.
 *
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   context.set_mask mask
 *   context.mask = mask
 *
 */
static VALUE ctx_set_mask(VALUE self, VALUE mask_o) {
  Pixmap *mask;
  Imlib_Context *ctx;

  Data_Get_Struct(self, Imlib_Context, ctx);
  Data_Get_Struct(mask_o, Pixmap, mask);

  imlib_context_push(*ctx);
  imlib_context_set_mask(*mask);
  imlib_context_pop();

  return mask_o;
}

/*
 * Get the current X11 Mask.
 * 
 * Note: This method is not available unless Imlib2-Ruby was compiled
 * with X11 support.  You can check the constant Imlib2::X11_SUPPORT to
 * see if X11 support is available.
 *
 * Examples:
 *   mask = context.get_mask
 *   mask = context.mask
 *
 */
static VALUE ctx_mask(VALUE self) {
  Imlib_Context *ctx;
  Pixmap *pmap;
  VALUE mask;

  Data_Get_Struct(self, Imlib_Context, ctx);
  pmap = malloc(sizeof(Pixmap));

  imlib_context_push(*ctx);
  *pmap = imlib_context_get_mask();
  mask = Data_Wrap_Struct(cPixmap, NULL, pmap_free, pmap);
  imlib_context_pop();

  return mask;
}
#endif /* !X_DISPLAY_MISSING */

/******************/
/* INIT FUNCTIONS */
/******************/
void setup_color_constants(void) {
  static struct {
    char *name;
    int r, g, b, a;
  } color_constants[] = {
    { "CLEAR",      0,   0,   0,   0   }, 
    { "TRANSPARENT",0,   0,   0,   0   }, 
    { "TRANSLUCENT",0,   0,   0,   0   }, 
    { "SHADOW",     0,   0,   0,   64  },

    { "BLACK",      0,   0,   0,   255 },
    { "DARKGRAY",   64,  64,  64,  255 },
    { "DARKGREY",   64,  64,  64,  255 },
    { "GRAY",       128, 128, 128, 255 },
    { "GREY",       128, 128, 128, 255 },
    { "LIGHTGRAY",  192, 192, 192, 255 },
    { "LIGHTGREY",  192, 192, 192, 255 },
    { "WHITE",      255, 255, 255, 255 },

    { "RED",        255, 0,   0,   255 },
    { "GREEN",      0,   255, 0,   255 },
    { "BLUE",       0,   0,   255, 255 },
    { "YELLOW",     255, 255, 0,   255 },
    { "ORANGE",     255, 128, 0,   255 },
    { "BROWN",      128, 64,  0,   255 },
    { "MAGENTA",    255, 0,   128, 255 },
    { "VIOLET",     255, 0,   255, 255 },
    { "PURPLE",     128, 0,   255, 255 },
    { "INDEGO",     128, 0,   255, 255 },
    { "CYAN",       0,   255, 255, 255 },
    { "AQUA",       0,   128, 255, 255 },
    { "AZURE",      0,   128, 255, 255 },
    { "TEAL",       0,   255, 128, 255 },

    { "DARKRED",    128, 0,   0,   255 },
    { "DARKGREEN",  0,   128, 0,   255 },
    { "DARKBLUE",   0,   0,   128, 255 },
    { "DARKYELLOW", 128, 128, 0,   255 },
    { "DARKORANGE", 128, 64,  0,   255 },
    { "DARKBROWN",  64,  32,  0,   255 },
    { "DARKMAGENTA",128, 0,   64,  255 },
    { "DARKVIOLET", 128, 0,   128, 255 },
    { "DARKPURPLE", 64,  0,   128, 255 },
    { "DARKINDEGO", 64,  0,   128, 255 },
    { "DARKCYAN",   0,   128, 128, 255 },
    { "DARKAQUA",   0,   64,  128, 255 },
    { "DARKAZURE",  0,   64,  128, 255 },
    { "DARKTEAL",   0,   128, 64,  255 },

    { NULL,         0,   0,   0,   0   }
  };
  int i;
  VALUE args[4];

  for (i = 0; color_constants[i].name != NULL; i++) {
    /* fprintf(stderr, "DEBUG: adding %s [%d, %d, %d, %d]\n", 
            color_constants[i].name,
            color_constants[i].r,
            color_constants[i].g,
            color_constants[i].b,
            color_constants[i].a);*/
    args[0] = INT2FIX(color_constants[i].r);
    args[1] = INT2FIX(color_constants[i].g);
    args[2] = INT2FIX(color_constants[i].b);
    args[3] = INT2FIX(color_constants[i].a);
    rb_define_const(mColor,
                    color_constants[i].name,
                    rgba_color_new(4, args, cRgbaColor));
  }
}

void setup_error_constants(void) {
  int i = 0;

  cFileError = rb_define_class_under(mImlib2, "FileError", rb_eException);
  mError = rb_define_module_under(mImlib2, "Error");
  cDeletedError = rb_define_class_under(mError, "DeletedError", rb_eException);

  for (i = 0; i <= IMLIB_LOAD_ERROR_UNKNOWN; i++)
    imlib_errors[i].exception = rb_define_class_under(mError,
                                                      imlib_errors[i].name,
                                                      cFileError);
}

void Init_imlib2() {
  /* fprintf(stderr, "DEBUG: Loading Imlib2.so...\n"); */
  mImlib2 = rb_define_module("Imlib2");
  rb_define_const(mImlib2, "VERSION", rb_str_new2(VERSION));

#ifdef X_DISPLAY_MISSING
  rb_define_const(mImlib2, "X11_SUPPORT", Qfalse);
#else
  rb_define_const(mImlib2, "X11_SUPPORT", Qtrue);
#endif /* X_DISPLAY_MISSING */

  /************************/
  /* define Context class */
  /************************/
  cContext = rb_define_class_under(mImlib2, "Context", rb_cObject);
  rb_define_singleton_method(cContext, "new", ctx_new, 0);
  rb_define_method(cContext, "initialize", ctx_init, 0);
  
  /* context stack methods */
  rb_define_method(cContext, "push", ctx_push, 0);
  rb_define_singleton_method(cContext, "pop", ctx_pop, 0);
  rb_define_singleton_method(cContext, "get", ctx_get, 0);
  rb_define_singleton_method(cContext, "current", ctx_get, 0);

  rb_define_method(cContext, "set_dither", ctx_set_dither, 1);
  rb_define_method(cContext, "dither=", ctx_set_dither, 1);
  rb_define_method(cContext, "get_dither", ctx_dither, 0);
  rb_define_method(cContext, "dither", ctx_dither, 0);
  rb_define_method(cContext, "set_dither_mask", ctx_set_dither_mask, 1);
  rb_define_method(cContext, "dither_mask=", ctx_set_dither_mask, 1);
  rb_define_method(cContext, "get_dither_mask", ctx_dither_mask, 0);
  rb_define_method(cContext, "dither_mask", ctx_dither_mask, 0);

  rb_define_method(cContext, "set_anti_alias", ctx_set_aa, 1);
  rb_define_method(cContext, "anti_alias=", ctx_set_aa, 1);
  rb_define_method(cContext, "aa=", ctx_set_aa, 1);
  rb_define_method(cContext, "get_anti_alias", ctx_aa, 0);
  rb_define_method(cContext, "anti_alias", ctx_aa, 0);
  rb_define_method(cContext, "aa", ctx_aa, 0);

  rb_define_method(cContext, "set_blend", ctx_set_blend, 1);
  rb_define_method(cContext, "blend=", ctx_set_blend, 1);
  rb_define_method(cContext, "get_blend", ctx_blend, 0);
  rb_define_method(cContext, "blend", ctx_blend, 0);

  rb_define_method(cContext, "set_color_modifier", ctx_set_cmod, 1);
  rb_define_method(cContext, "color_modifier=", ctx_set_cmod, 1);
  rb_define_method(cContext, "cmod=", ctx_set_cmod, 1);
  rb_define_method(cContext, "get_color_modifier", ctx_cmod, 0);
  rb_define_method(cContext, "color_modifier", ctx_cmod, 0);
  rb_define_method(cContext, "cmod", ctx_cmod, 0);

  rb_define_method(cContext, "set_operation", ctx_set_op, 1);
  rb_define_method(cContext, "operation=", ctx_set_op, 1);
  rb_define_method(cContext, "op=", ctx_set_op, 1);
  rb_define_method(cContext, "get_operation", ctx_op, 0);
  rb_define_method(cContext, "operation", ctx_op, 0);
  rb_define_method(cContext, "op", ctx_op, 0);

  rb_define_method(cContext, "set_font", ctx_set_font, 1);
  rb_define_method(cContext, "font=", ctx_set_font, 1);
  rb_define_method(cContext, "get_font", ctx_font, 0);
  rb_define_method(cContext, "font", ctx_font, 0);

  rb_define_method(cContext, "set_direction", ctx_set_dir, 1);
  rb_define_method(cContext, "direction=", ctx_set_dir, 1);
  rb_define_method(cContext, "dir=", ctx_set_dir, 1);
  rb_define_method(cContext, "get_direction", ctx_dir, 0);
  rb_define_method(cContext, "direction", ctx_dir, 0);
  rb_define_method(cContext, "dir", ctx_dir, 0);

  rb_define_method(cContext, "set_angle", ctx_set_angle, 1);
  rb_define_method(cContext, "angle=", ctx_set_angle, 1);
  rb_define_method(cContext, "get_angle", ctx_angle, 0);
  rb_define_method(cContext, "angle", ctx_angle, 0);

  rb_define_method(cContext, "set_color", ctx_set_color, 1);
  rb_define_method(cContext, "color=", ctx_set_color, 1);
  rb_define_method(cContext, "get_color", ctx_color, 0);
  rb_define_method(cContext, "color", ctx_color, 0);

  /* FIXME: add support for multiple color types */

  /* maybe add aliases for color_range? */
  rb_define_method(cContext, "set_gradient", ctx_set_gradient, 1);
  rb_define_method(cContext, "gradient=", ctx_set_gradient, 1);
  rb_define_method(cContext, "get_gradient", ctx_gradient, 0);
  rb_define_method(cContext, "gradient", ctx_gradient, 0);

  rb_define_method(cContext, "set_progress_granularity", ctx_set_progress_granularity, 1);
  rb_define_method(cContext, "progress_granularity=", ctx_set_progress_granularity, 1);
  rb_define_method(cContext, "get_progress_granularity", ctx_progress_granularity, 0);
  rb_define_method(cContext, "progress_granularity", ctx_progress_granularity, 0);

  rb_define_method(cContext, "set_image", ctx_set_image, 1);
  rb_define_method(cContext, "image=", ctx_set_image, 1);
  rb_define_method(cContext, "get_image", ctx_image, 0);
  rb_define_method(cContext, "image", ctx_image, 0);

  rb_define_method(cContext, "set_cliprect", ctx_set_cliprect, 1);
  rb_define_method(cContext, "cliprect=", ctx_set_cliprect, 1);
  rb_define_method(cContext, "get_cliprect", ctx_cliprect, 0);
  rb_define_method(cContext, "cliprect", ctx_cliprect, 0);

  rb_define_method(cContext, "set_ttf_encoding", ctx_set_encoding, 1);
  rb_define_method(cContext, "set_encoding", ctx_set_encoding, 1);
  rb_define_method(cContext, "encoding=", ctx_set_encoding, 1);
  rb_define_method(cContext, "get_encoding", ctx_encoding, 0);
  rb_define_method(cContext, "get_ttf_encoding", ctx_encoding, 0);
  rb_define_method(cContext, "encoding", ctx_encoding, 0);


#ifndef X_DISPLAY_MISSING
  /* context X11 methods */
  rb_define_method(cContext, "set_display", ctx_set_display, 1);
  rb_define_method(cContext, "display=", ctx_set_display, 1);
  rb_define_method(cContext, "get_display", ctx_display, 0);
  rb_define_method(cContext, "display", ctx_display, 0);

  rb_define_method(cContext, "set_visual", ctx_set_visual, 1);
  rb_define_method(cContext, "visual=", ctx_set_visual, 1);
  rb_define_method(cContext, "get_visual", ctx_visual, 0);
  rb_define_method(cContext, "visual", ctx_visual, 0);

  rb_define_method(cContext, "set_colormap", ctx_set_cmap, 1);
  rb_define_method(cContext, "colormap=", ctx_set_cmap, 1);
  rb_define_method(cContext, "set_cmap", ctx_set_cmap, 1);
  rb_define_method(cContext, "cmap=", ctx_set_cmap, 1);
  rb_define_method(cContext, "get_colormap", ctx_cmap, 0);
  rb_define_method(cContext, "colormap", ctx_cmap, 0);
  rb_define_method(cContext, "get_cmap", ctx_cmap, 0);
  rb_define_method(cContext, "cmap", ctx_cmap, 0);

  rb_define_method(cContext, "set_drawable", ctx_set_drawable, 1);
  rb_define_method(cContext, "drawable=", ctx_set_drawable, 1);
  rb_define_method(cContext, "get_drawable", ctx_drawable, 0);
  rb_define_method(cContext, "drawable", ctx_drawable, 0);

  rb_define_method(cContext, "set_mask", ctx_set_mask, 1);
  rb_define_method(cContext, "mask=", ctx_set_mask, 1);
  rb_define_method(cContext, "get_mask", ctx_mask, 0);
  rb_define_method(cContext, "mask", ctx_mask, 0);

  rb_define_method(cContext, "get_visual", ctx_visual, -1);
  rb_define_method(cContext, "visual", ctx_visual, -1);

  rb_define_method(cContext, "get_best_visual", ctx_visual, -1);
  rb_define_method(cContext, "best_visual", ctx_visual, -1);
#endif /* X_DISPLAY_MISSING */

  /**********************/
  /* define Error class */
  /**********************/
  setup_error_constants();
  
  /*********************/
  /* define Dir module */
  /*********************/
  mTextDir = rb_define_module_under(mImlib2, "Dir");
  rb_define_const(mTextDir, "RIGHT", INT2FIX(IMLIB_TEXT_TO_RIGHT));
  rb_define_const(mTextDir, "LEFT", INT2FIX(IMLIB_TEXT_TO_LEFT));
  rb_define_const(mTextDir, "DOWN", INT2FIX(IMLIB_TEXT_TO_DOWN));
  rb_define_const(mTextDir, "UP", INT2FIX(IMLIB_TEXT_TO_UP));
  rb_define_const(mTextDir, "ANGLE", INT2FIX(IMLIB_TEXT_TO_ANGLE));
  
  /***************************/
  /* define Direction module */
  /* (can't alias modules)   */
  /***************************/
  mTextDir = rb_define_module_under(mImlib2, "Direction");
  rb_define_const(mTextDir, "RIGHT", INT2FIX(IMLIB_TEXT_TO_RIGHT));
  rb_define_const(mTextDir, "LEFT", INT2FIX(IMLIB_TEXT_TO_LEFT));
  rb_define_const(mTextDir, "DOWN", INT2FIX(IMLIB_TEXT_TO_DOWN));
  rb_define_const(mTextDir, "UP", INT2FIX(IMLIB_TEXT_TO_UP));
  rb_define_const(mTextDir, "ANGLE", INT2FIX(IMLIB_TEXT_TO_ANGLE));
  
  /********************/
  /* define Op module */
  /********************/
  mOp = rb_define_module_under(mImlib2, "Op");
  rb_define_const(mOp, "COPY", INT2FIX(IMLIB_OP_COPY));
  rb_define_const(mOp, "ADD", INT2FIX(IMLIB_OP_ADD));
  rb_define_const(mOp, "SUBTRACT", INT2FIX(IMLIB_OP_SUBTRACT));
  rb_define_const(mOp, "RESHADE", INT2FIX(IMLIB_OP_RESHADE));
  
  /*******************************/
  /* define Operation module     */
  /* (can't alias modules.. yet) */
  /*******************************/
  mOperation = rb_define_module_under(mImlib2, "Operation");
  rb_define_const(mOperation, "COPY", INT2FIX(IMLIB_OP_COPY));
  rb_define_const(mOperation, "ADD", INT2FIX(IMLIB_OP_ADD));
  rb_define_const(mOperation, "SUBTRACT", INT2FIX(IMLIB_OP_SUBTRACT));
  rb_define_const(mOperation, "RESHADE", INT2FIX(IMLIB_OP_RESHADE));
  
  /**************************/
  /* define Encoding module */
  /**************************/
  mEncoding = rb_define_module_under(mImlib2, "Encoding");
  rb_define_const(mEncoding, "ISO_8859_1", INT2FIX(IMLIB_TTF_ENCODING_ISO_8859_1));
  rb_define_const(mEncoding, "ISO_8859_2", INT2FIX(IMLIB_TTF_ENCODING_ISO_8859_2));
  rb_define_const(mEncoding, "ISO_8859_3", INT2FIX(IMLIB_TTF_ENCODING_ISO_8859_3));
  rb_define_const(mEncoding, "ISO_8859_4", INT2FIX(IMLIB_TTF_ENCODING_ISO_8859_4));
  rb_define_const(mEncoding, "ISO_8859_5", INT2FIX(IMLIB_TTF_ENCODING_ISO_8859_5));
  
  /***********************/
  /* define Border class */
  /***********************/
  cBorder = rb_define_class_under(mImlib2, "Border", rb_cObject);
  rb_define_singleton_method(cBorder, "new", border_new, -1);
  rb_define_method(cBorder, "initialize", border_init, -1);
  /* FIXME rb_define_method(cBorder, "[]", border_ary, 0);
  rb_define_method(cBorder, "[]=", border_set_ary, 2); */

  rb_define_method(cBorder, "left", border_left, 0);
  rb_define_method(cBorder, "left=", border_set_left, 1);
  rb_define_alias(cBorder, "l", "left");
  rb_define_alias(cBorder, "l=", "left=");

  rb_define_method(cBorder, "right", border_right, 0);
  rb_define_method(cBorder, "right=", border_set_right, 1);
  rb_define_alias(cBorder, "r", "right");
  rb_define_alias(cBorder, "r=", "right=");

  rb_define_method(cBorder, "top", border_top, 0);
  rb_define_method(cBorder, "top=", border_set_top, 1);
  rb_define_alias(cBorder, "t", "top");
  rb_define_alias(cBorder, "t=", "top=");

  rb_define_method(cBorder, "bottom", border_bottom, 0);
  rb_define_method(cBorder, "bottom=", border_set_bottom, 1);
  rb_define_alias(cBorder, "b", "bottom");
  rb_define_alias(cBorder, "b=", "bottom=");

  /***********************/
  /* define Cache module */
  /***********************/
  mCache  = rb_define_module_under(mImlib2, "Cache"); 
  rb_define_singleton_method(mCache, "image", cache_image, 0);
  rb_define_singleton_method(mCache, "image=", cache_set_image, 1);
  rb_define_singleton_method(mCache, "image_cache", cache_image, 0);
  rb_define_singleton_method(mCache, "image_cache=", cache_set_image, 1);
  rb_define_singleton_method(mCache, "get_image_cache", cache_image, 0);
  rb_define_singleton_method(mCache, "set_image_cache", cache_set_image, 1);

  rb_define_singleton_method(mCache, "font", cache_font, 0);
  rb_define_singleton_method(mCache, "font=", cache_set_font, 1);
  rb_define_singleton_method(mCache, "font_cache", cache_font, 0);
  rb_define_singleton_method(mCache, "font_cache=", cache_set_font, 1);
  rb_define_singleton_method(mCache, "get_font_cache", cache_font, 0);
  rb_define_singleton_method(mCache, "set_font_cache", cache_set_font, 1);
  rb_define_singleton_method(mCache, "flush_font_cache", cache_flush_font, 0);

  /***********************/
  /* define Color module */
  /***********************/
  mColor  = rb_define_module_under(mImlib2, "Color"); 

  /***************************/
  /* define RGBA Color class */
  /***************************/
  cRgbaColor = rb_define_class_under(mColor, "RgbaColor", rb_cObject);
  rb_define_singleton_method(cRgbaColor, "new", rgba_color_new, -1);
  rb_define_method(cRgbaColor, "initialize", rgba_color_init, -1);
  /* FIXME rb_define_method(cRgbaColor, "[]", rgba_color_ary, 0);
  rb_define_method(cRgbaColor, "[]=", rgba_color_set_ary, 2); */
  rb_define_method(cRgbaColor, "r", rgba_color_red, 0);
  rb_define_method(cRgbaColor, "r=", rgba_color_set_red, 1);
  rb_define_method(cRgbaColor, "red", rgba_color_red, 0);
  rb_define_method(cRgbaColor, "red=", rgba_color_set_red, 1);
  rb_define_method(cRgbaColor, "g", rgba_color_green, 0);
  rb_define_method(cRgbaColor, "g=", rgba_color_set_green, 1);
  rb_define_method(cRgbaColor, "green", rgba_color_green, 0);
  rb_define_method(cRgbaColor, "green=", rgba_color_set_green, 1);
  rb_define_method(cRgbaColor, "b", rgba_color_blue, 0);
  rb_define_method(cRgbaColor, "b=", rgba_color_set_blue, 1);
  rb_define_method(cRgbaColor, "blue", rgba_color_blue, 0);
  rb_define_method(cRgbaColor, "blue=", rgba_color_set_blue, 1);
  rb_define_method(cRgbaColor, "a", rgba_color_alpha, 0);
  rb_define_method(cRgbaColor, "a=", rgba_color_set_alpha, 1);
  rb_define_method(cRgbaColor, "alpha", rgba_color_alpha, 0);
  rb_define_method(cRgbaColor, "alpha=", rgba_color_set_alpha, 1);

  /***************************/
  /* define HSVA Color class */
  /***************************/
  cHsvaColor = rb_define_class_under(mColor, "HsvaColor", rb_cObject);
  rb_define_singleton_method(cHsvaColor, "new", hsva_color_new, -1);
  rb_define_method(cHsvaColor, "initialize", hsva_color_init, -1);
  /* FIXME rb_define_method(cHsvaColor, "[]", hsva_color_ary, 0);
  rb_define_method(cHsvaColor, "[]=", hsva_color_set_ary, 2); */
  rb_define_method(cHsvaColor, "h", hsva_color_hue, 0);
  rb_define_method(cHsvaColor, "h=", hsva_color_set_hue, 1);
  rb_define_method(cHsvaColor, "hue", hsva_color_hue, 0);
  rb_define_method(cHsvaColor, "hue=", hsva_color_set_hue, 1);
  rb_define_method(cHsvaColor, "s", hsva_color_saturation, 0);
  rb_define_method(cHsvaColor, "s=", hsva_color_set_saturation, 1);
  rb_define_method(cHsvaColor, "saturation", hsva_color_saturation, 0);
  rb_define_method(cHsvaColor, "saturation=", hsva_color_set_saturation, 1);
  rb_define_method(cHsvaColor, "v", hsva_color_value, 0);
  rb_define_method(cHsvaColor, "v=", hsva_color_set_value, 1);
  rb_define_method(cHsvaColor, "value", hsva_color_value, 0);
  rb_define_method(cHsvaColor, "value=", hsva_color_set_value, 1);
  rb_define_method(cHsvaColor, "a", hsva_color_alpha, 0);
  rb_define_method(cHsvaColor, "a=", hsva_color_set_alpha, 1);
  rb_define_method(cHsvaColor, "alpha", hsva_color_alpha, 0);
  rb_define_method(cHsvaColor, "alpha=", hsva_color_set_alpha, 1);

  /***************************/
  /* define HSVA Color class */
  /***************************/
  cHlsaColor = rb_define_class_under(mColor, "HlsaColor", rb_cObject);
  rb_define_singleton_method(cHlsaColor, "new", hlsa_color_new, -1);
  rb_define_method(cHlsaColor, "initialize", hlsa_color_init, -1);
  /* FIXME rb_define_method(cHlsaColor, "[]", hlsa_color_ary, 0);
  rb_define_method(cHlsaColor, "[]=", hlsa_color_set_ary, 2); */
  rb_define_method(cHlsaColor, "h", hlsa_color_hue, 0);
  rb_define_method(cHlsaColor, "h=", hlsa_color_set_hue, 1);
  rb_define_method(cHlsaColor, "hue", hlsa_color_hue, 0);
  rb_define_method(cHlsaColor, "hue=", hlsa_color_set_hue, 1);
  rb_define_method(cHlsaColor, "l", hlsa_color_lightness, 0);
  rb_define_method(cHlsaColor, "l=", hlsa_color_set_lightness, 1);
  rb_define_method(cHlsaColor, "lightness", hlsa_color_lightness, 0);
  rb_define_method(cHlsaColor, "lightness=", hlsa_color_set_lightness, 1);
  rb_define_method(cHlsaColor, "s", hlsa_color_saturation, 0);
  rb_define_method(cHlsaColor, "s=", hlsa_color_set_saturation, 1);
  rb_define_method(cHlsaColor, "saturation", hlsa_color_saturation, 0);
  rb_define_method(cHlsaColor, "saturation=", hlsa_color_set_saturation, 1);
  rb_define_method(cHlsaColor, "a", hlsa_color_alpha, 0);
  rb_define_method(cHlsaColor, "a=", hlsa_color_set_alpha, 1);
  rb_define_method(cHlsaColor, "alpha", hlsa_color_alpha, 0);
  rb_define_method(cHlsaColor, "alpha=", hlsa_color_set_alpha, 1);

  /***************************/
  /* define CMYA Color class */
  /***************************/
  cCmyaColor = rb_define_class_under(mColor, "CmyaColor", rb_cObject);
  rb_define_singleton_method(cCmyaColor, "new", cmya_color_new, -1);
  rb_define_method(cCmyaColor, "initialize", cmya_color_init, -1);
  /* FIXME rb_define_method(cCmyaColor, "[]", cmya_color_ary, 0);
  rb_define_method(cCmyaColor, "[]=", cmya_color_set_ary, 2); */
  rb_define_method(cCmyaColor, "c", cmya_color_cyan, 0);
  rb_define_method(cCmyaColor, "c=", cmya_color_set_cyan, 1);
  rb_define_method(cCmyaColor, "cyan", cmya_color_cyan, 0);
  rb_define_method(cCmyaColor, "cyan=", cmya_color_set_cyan, 1);
  rb_define_method(cCmyaColor, "m", cmya_color_magenta, 0);
  rb_define_method(cCmyaColor, "m=", cmya_color_set_magenta, 1);
  rb_define_method(cCmyaColor, "magenta", cmya_color_magenta, 0);
  rb_define_method(cCmyaColor, "magenta=", cmya_color_set_magenta, 1);
  rb_define_method(cCmyaColor, "y", cmya_color_yellow, 0);
  rb_define_method(cCmyaColor, "y=", cmya_color_set_yellow, 1);
  rb_define_method(cCmyaColor, "yellow", cmya_color_yellow, 0);
  rb_define_method(cCmyaColor, "yellow=", cmya_color_set_yellow, 1);
  rb_define_method(cCmyaColor, "a", cmya_color_alpha, 0);
  rb_define_method(cCmyaColor, "a=", cmya_color_set_alpha, 1);
  rb_define_method(cCmyaColor, "alpha", cmya_color_alpha, 0);
  rb_define_method(cCmyaColor, "alpha=", cmya_color_set_alpha, 1);

  /**************************/
  /* define Color constants */
  /**************************/
  setup_color_constants();
  
  /*************************/
  /* define ColorMod class */
  /*************************/
  cColorMod = rb_define_class_under(mImlib2, "ColorModifier", rb_cObject);
  rb_define_singleton_method(cColorMod, "new", cmod_new, -1);
  rb_define_method(cColorMod, "initialize", cmod_init, -1);
  rb_define_method(cColorMod, "gamma=", cmod_gamma, 1);
  rb_define_method(cColorMod, "brightness=", cmod_brightness, 1);
  rb_define_method(cColorMod, "contrast=", cmod_contrast, 1);
  rb_define_method(cColorMod, "reset", cmod_reset, 0);

  /*************************/
  /* define Gradient class */
  /*************************/
  cGradient = rb_define_class_under(mImlib2, "Gradient", rb_cObject);
  rb_define_singleton_method(cGradient, "new", gradient_new, -1);
  rb_define_method(cGradient, "initialize", gradient_init, -1);
  rb_define_method(cGradient, "add_color", gradient_add_color, 2);

  /* hack: alias should work with modules and classes as well :) */
  /* disabled: this breaks with Rails (and probably other stuff */
/* 
 *   rb_eval_string(
 *     "module Imlib2\n"
 *     "  class ColorRange < Gradient\n"
 *     "  end\n"
 *     "end\n"
 *   );
 */ 

  /**********************/
  /* define Image class */
  /**********************/
  cImage   = rb_define_class_under(mImlib2, "Image", rb_cObject);
  rb_define_singleton_method(cImage, "new", image_new, 2);
  rb_define_method(cImage, "initialize", image_initialize, 0);

  /* workarounds */
  rb_define_singleton_method(cImage, "draw_pixel_workaround?", image_dp_workaround, 0);
  rb_define_singleton_method(cImage, "bypass_draw_pixel?", image_dp_workaround, 0);
  rb_define_singleton_method(cImage, "draw_pixel_workaround=", image_dp_workaround, 1);
  rb_define_singleton_method(cImage, "bypass_draw_pixel=", image_dp_workaround, 1);

  /* create methods */
  rb_define_singleton_method(cImage, "create", image_new, 2);
  rb_define_singleton_method(cImage, "create_using_data", image_create_using_data, 3);
  rb_define_singleton_method(cImage, "create_using_copied_data", image_create_using_copied_data, 3);

  /* load methods */
  rb_define_singleton_method(cImage, "load", image_load, 1);
  rb_define_singleton_method(cImage, "load_image", image_load_image, 1);
  rb_define_singleton_method(cImage, "load_immediately", image_load_immediately, 1);
  rb_define_singleton_method(cImage, "load_without_cache", image_load_without_cache, 1);
  rb_define_singleton_method(cImage, "load_immediately_without_cache", image_load_immediately_without_cache, 1);
  rb_define_singleton_method(cImage, "load_with_error_return", image_load_with_error_return, 1);

  /* save methods */
  rb_define_method(cImage, "save", image_save, 1);
  rb_define_method(cImage, "save_image", image_save_image, 1);
  rb_define_method(cImage, "save_with_error_return", image_save_with_error_return, 1);

  /* delete method */
  rb_define_method(cImage, "delete!", image_delete, -1);
  
  /* member methods */
  rb_define_method(cImage, "width", image_width, 0);
  rb_define_method(cImage, "w", image_width, 0);
  rb_define_method(cImage, "height", image_height, 0);
  rb_define_method(cImage, "h", image_height, 0);
  rb_define_method(cImage, "filename", image_filename, 0);

  rb_define_method(cImage, "data", image_data, 0);
  rb_define_method(cImage, "data_for_reading_only", image_data_ro, 0);
  rb_define_method(cImage, "data!", image_data_ro, 0);
  rb_define_method(cImage, "data=", image_put_data, 1);
  rb_define_method(cImage, "put_back_data", image_put_data, 1);

  rb_define_method(cImage, "has_alpha", image_has_alpha, 0);
  rb_define_method(cImage, "has_alpha?", image_has_alpha, 0);
  rb_define_method(cImage, "has_alpha=", image_set_has_alpha, 1);
  rb_define_method(cImage, "set_has_alpha", image_set_has_alpha, 1);

  rb_define_method(cImage, "changes_on_disk", image_changes_on_disk, 0);
  rb_define_method(cImage, "set_changes_on_disk", image_changes_on_disk, 0);

  rb_define_method(cImage, "border", image_get_border, 0);
  rb_define_method(cImage, "get_border", image_get_border, 0);
  rb_define_method(cImage, "border=", image_set_border, 1);
  rb_define_method(cImage, "set_border", image_set_border, 1);

  rb_define_method(cImage, "format", image_get_format, 0);
  rb_define_method(cImage, "get_format", image_get_format, 0);
  rb_define_method(cImage, "format=", image_set_format, 1);
  rb_define_method(cImage, "set_format", image_set_format, 1);

  rb_define_method(cImage, "irrelevant_format=", image_irrelevant_format, 1);
  rb_define_method(cImage, "set_irrelevant_format", image_irrelevant_format, 1);
  rb_define_method(cImage, "irrelevant_border=", image_irrelevant_border, 1);
  rb_define_method(cImage, "set_irrelevant_border", image_irrelevant_border, 1);
  rb_define_method(cImage, "irrelevant_alpha=", image_irrelevant_alpha, 1);
  rb_define_method(cImage, "set_irrelevant_alpha", image_irrelevant_alpha, 1);

  rb_define_method(cImage, "pixel", image_query_pixel, 2);
  rb_define_method(cImage, "pixel_rgba", image_query_pixel, 2);
  rb_define_method(cImage, "query_pixel", image_query_pixel, 2);
  rb_define_method(cImage, "query_pixel_rgba", image_query_pixel, 2);
  rb_define_method(cImage, "pixel_hsva", image_query_pixel_hsva, 2);
  rb_define_method(cImage, "query_pixel_hsva", image_query_pixel_hsva, 2);
  rb_define_method(cImage, "pixel_hlsa", image_query_pixel_hlsa, 2);
  rb_define_method(cImage, "query_pixel_hlsa", image_query_pixel_hlsa, 2);
  rb_define_method(cImage, "pixel_cmya", image_query_pixel_cmya, 2);
  rb_define_method(cImage, "query_pixel_cmya", image_query_pixel_cmya, 2);

  /* more create methods */
  rb_define_method(cImage, "crop", image_crop, -1);
  rb_define_method(cImage, "create_cropped", image_crop, -1);
  rb_define_method(cImage, "crop!", image_crop_inline, -1);
  rb_define_method(cImage, "create_cropped!", image_crop_inline, -1);
  rb_define_method(cImage, "crop_scaled", image_crop_scaled, -1);
  rb_define_method(cImage, "create_cropped_scaled", image_crop_scaled, -1);
  rb_define_method(cImage, "crop_scaled!", image_crop_scaled_inline, -1);
  rb_define_method(cImage, "create_cropped_scaled!", image_crop_scaled_inline, -1);

  /* image modification methods */
  rb_define_method(cImage, "flip_horizontal", image_flip_horizontal, 0);
  rb_define_method(cImage, "flip_horizontal!", image_flip_horizontal_inline, 0);
  rb_define_method(cImage, "flip_vertical", image_flip_vertical, 0);
  rb_define_method(cImage, "flip_vertical!", image_flip_vertical_inline, 0);
  rb_define_method(cImage, "flip_diagonal", image_flip_diagonal, 0);
  rb_define_method(cImage, "flip_diagonal!", image_flip_diagonal_inline, 0);

  rb_define_method(cImage, "orientate", image_orientate, 1);
  rb_define_method(cImage, "orientate!", image_orientate_inline, 1);
  rb_define_method(cImage, "blur", image_blur, 1);
  rb_define_method(cImage, "blur!", image_blur_inline, 1);
  rb_define_method(cImage, "sharpen", image_sharpen, 1);
  rb_define_method(cImage, "sharpen!", image_sharpen_inline, 1);

  rb_define_method(cImage, "tile_horizontal", image_tile_horizontal, 0);
  rb_define_method(cImage, "tile_horizontal!", image_tile_horizontal_inline, 0);
  rb_define_method(cImage, "tile_vertical", image_tile_vertical, 0);
  rb_define_method(cImage, "tile_vertical!", image_tile_vertical_inline, 0);
  rb_define_method(cImage, "tile", image_tile, 0);
  rb_define_method(cImage, "tile!", image_tile_inline, 0);

  /* image drawing methods */
  rb_define_method(cImage, "draw_pixel", image_draw_pixel, -1);
  rb_define_method(cImage, "draw_line", image_draw_line, -1);
  /* FIXME: rb_define_method(cImage, "clip_line", image_clip_line, -1);*/
  rb_define_method(cImage, "draw_rect", image_draw_rect, -1);
  rb_define_method(cImage, "draw_rectangle", image_draw_rect, -1);
  rb_define_method(cImage, "fill_rect", image_fill_rect, -1);
  rb_define_method(cImage, "fill_rectangle", image_fill_rect, -1);
  rb_define_method(cImage, "copy_alpha", image_copy_alpha, -1);
  rb_define_method(cImage, "copy_alpha_rect", image_copy_alpha_rect, -1);
  rb_define_method(cImage, "scroll_rect", image_scroll_rect, -1);
  rb_define_method(cImage, "copy_rect", image_copy_rect, -1);

  /* ellipse drawing methods */
  rb_define_method(cImage, "draw_ellipse", image_draw_ellipse, -1);
  rb_define_method(cImage, "draw_oval", image_draw_ellipse, -1);
  rb_define_method(cImage, "fill_ellipse", image_fill_ellipse, -1);
  rb_define_method(cImage, "fill_oval", image_fill_ellipse, -1);

  /* text drawing methods */
  rb_define_method(cImage, "draw_text", image_draw_text, -1);

  /* gradient (color range) drawing methods */
  rb_define_method(cImage, "gradient", image_fill_gradient, -1);
  rb_define_method(cImage, "fill_gradient", image_fill_gradient, -1);
  rb_define_method(cImage, "color_range", image_fill_gradient, -1);
  rb_define_method(cImage, "fill_color_range", image_fill_gradient, -1);

  /* polygon drawing methods */
  rb_define_method(cImage, "draw_poly", image_draw_poly, -1);
  rb_define_method(cImage, "draw_polygon", image_draw_poly, -1);
  rb_define_method(cImage, "fill_poly", image_fill_poly, -1);
  rb_define_method(cImage, "fill_polygon", image_fill_poly, -1);

  /* blend methods */
  rb_define_method(cImage, "blend!", image_blend_image_inline, -1);
  rb_define_method(cImage, "blend_image!", image_blend_image_inline, -1);
  rb_define_method(cImage, "blend", image_blend_image, -1);
  rb_define_method(cImage, "blend_image", image_blend_image, -1);

  /* rotation / skewing methods */
  rb_define_method(cImage, "rotate", image_rotate, 1);
  rb_define_method(cImage, "rotate!", image_rotate_inline, 1);

  /* misc methods */
  rb_define_method(cImage, "clone", image_clone, 0);
  rb_define_method(cImage, "dup", image_clone, 0);

  /* clear methods */
  rb_define_method(cImage, "clear", image_clear, 0);
  rb_define_method(cImage, "clear_color", image_clear_color, 1);
  rb_define_method(cImage, "clear_color!", image_clear_color_inline, 1);

  /* polymorphic (implicit) filter methods */
  rb_define_method(cImage, "filter", image_filter, 1);
  rb_define_method(cImage, "apply_filter", image_filter, 1);

  /* explicit filter methods */
  rb_define_method(cImage, "static_filter", image_static_filter, 1);
  rb_define_method(cImage, "script_filter", image_script_filter, 1);

  /* color modifier methods */
  rb_define_method(cImage, "apply_color_modifier", image_apply_cmod, 1);
  rb_define_method(cImage, "apply_cmod", image_apply_cmod, 1);
  rb_define_method(cImage, "apply", image_apply_cmod, 1);

  rb_define_method(cImage, "attach_value", image_attach_val, 2);
  rb_define_method(cImage, "get_attached_value", image_get_attach_val, 1);
  rb_define_method(cImage, "remove_attached_value", image_rm_attach_val, 1);

  rb_define_method(cImage, "[]", image_get_attach_val, 1);
  rb_define_method(cImage, "[]=", image_attach_val, 2);

  
  /* image X11 methods */
#ifndef X_DISPLAY_MISSING
  /* pixmap methods */
  /* wraps all three pixmap render calls */
  rb_define_method(cImage, "render_pixmap", image_render_pmap, -1);
  rb_define_method(cImage, "pixmap", image_render_pmap, -1);

  /* drawable methods */
  /* wraps all three drawable render calls */
  rb_define_method(cImage, "render_on_drawable", image_render_on_drawable, -1);

  /* X11 create methods */
  rb_define_singleton_method(cImage, "create_from_drawable", image_create_from_drawable, -1);
  rb_define_singleton_method(cImage, "create_from_ximage", image_create_from_ximage, -1);

  rb_define_method(cImage, "copy_drawable", image_copy_drawable, -1);
  rb_define_method(cImage, "blend_drawable", image_blend_drawable, -1);

  rb_define_method(cImage, "render_on_drawable_skewed", image_render_drawable_skewed, -1);
  rb_define_method(cImage, "render_on_drawable_at_angle", image_render_drawable_angle, -1);
#endif /* X_DISPLAY_MISSING */

  /***********************/
  /* define Filter class */
  /***********************/
  cFilter = rb_define_class_under(mImlib2, "Filter", rb_cObject);
  rb_define_singleton_method(cFilter, "new", filter_new, 1);
  rb_define_method(cFilter, "initialize", filter_init, 1);

  rb_define_method(cFilter, "set", filter_set, -1);
  rb_define_method(cFilter, "red", filter_set_red, -1);
  rb_define_method(cFilter, "set_red", filter_set_red, -1);
  rb_define_method(cFilter, "green", filter_set_green, -1);
  rb_define_method(cFilter, "set_green", filter_set_green, -1);
  rb_define_method(cFilter, "blue", filter_set_blue, -1);
  rb_define_method(cFilter, "set_blue", filter_set_blue, -1);
  rb_define_method(cFilter, "alpha", filter_set_alpha, -1);
  rb_define_method(cFilter, "set_alpha", filter_set_alpha, -1);

  rb_define_method(cFilter, "constants", filter_constants, 1);
  rb_define_method(cFilter, "divisors", filter_divisors, 1);

  /*********************/
  /* define Font class */
  /*********************/
  cFont    = rb_define_class_under(mImlib2, "Font", rb_cObject);
  rb_define_singleton_method(cFont, "new", font_new, 1);
  rb_define_singleton_method(cFont, "load", font_new, 1);
  rb_define_method(cFont, "initialize", font_init, 0);

  rb_define_method(cFont, "size", font_text_size, 1);
  rb_define_method(cFont, "text_size", font_text_size, 1);
  rb_define_method(cFont, "get_text_size", font_text_size, 1);
  rb_define_method(cFont, "advance", font_text_advance, 1);
  rb_define_method(cFont, "text_advance", font_text_advance, 1);
  rb_define_method(cFont, "get_text_advance", font_text_advance, 1);
  rb_define_method(cFont, "inset", font_text_inset, 1);
  rb_define_method(cFont, "text_inset", font_text_inset, 1);
  rb_define_method(cFont, "get_text_inset", font_text_inset, 1);

  rb_define_method(cFont, "index", font_text_index, -1);
  rb_define_method(cFont, "text_index", font_text_index, -1);
  rb_define_method(cFont, "text_index_and_location", font_text_index, -1);
  rb_define_method(cFont, "get_text_index_and_location", font_text_index, -1);
  rb_define_method(cFont, "location", font_text_location, 2);
  rb_define_method(cFont, "text_location", font_text_location, 2);
  rb_define_method(cFont, "text_location_at_index", font_text_location, 2);
  rb_define_method(cFont, "get_text_location_at_index", font_text_location, 2);

  rb_define_method(cFont, "ascent", font_ascent, 0);
  rb_define_method(cFont, "get_ascent", font_ascent, 0);
  rb_define_method(cFont, "descent", font_descent, 0);
  rb_define_method(cFont, "get_descent", font_descent, 0);
  rb_define_method(cFont, "maximum_ascent", font_maximum_ascent, 0);
  rb_define_method(cFont, "get_maximum_ascent", font_maximum_ascent, 0);
  rb_define_method(cFont, "maximum_descent", font_maximum_descent, 0);
  rb_define_method(cFont, "get_maximum_descent", font_maximum_descent, 0);

  /*******************************/
  /* define Font singletons      */
  /* (font list, and font paths) */
  /*******************************/
  rb_define_singleton_method(cFont, "list", font_list_fonts, 0);
  rb_define_singleton_method(cFont, "fonts", font_list_fonts, 0);
  rb_define_singleton_method(cFont, "list_fonts", font_list_fonts, 0);

  rb_define_singleton_method(cFont, "add_path", font_add_path, 1);
  rb_define_singleton_method(cFont, "remove_path", font_remove_path, 1);
  rb_define_singleton_method(cFont, "paths", font_list_paths, 0);
  rb_define_singleton_method(cFont, "list_paths", font_list_paths, 0);

  /*********************/
  /* define Font class */
  /*********************/
  cPolygon = rb_define_class_under(mImlib2, "Polygon", rb_cObject);
  rb_define_singleton_method(cPolygon, "new", poly_new, -1);
  rb_define_method(cPolygon, "initialize", poly_init, -1);
  rb_define_method(cPolygon, "add_point", poly_add_point, -1);
  rb_define_method(cPolygon, "bounds", poly_bounds, 0);
  rb_define_method(cPolygon, "get_bounds", poly_bounds, 0);
  rb_define_method(cPolygon, "contains?", poly_contains, -1);
  rb_define_method(cPolygon, "contains_point?", poly_contains, -1);

#ifndef X_DISPLAY_MISSING
  /*********************/
  /* define X11 module */
  /*********************/
  mX11 = rb_define_module_under(mImlib2, "X11", rb_cObject);

  cDisplay = rb_define_class_under(mX11, "Display", rb_cObject);
  rb_define_singleton_method(cDisplay, "new", disp_new, 0);
  rb_define_method(cDisplay, "initialize", disp_init, 0);

  cVisual = rb_define_class_under(mX11, "Visual", rb_cObject);
  rb_define_singleton_method(cVisual, "new", vis_new, 2);
  rb_define_method(cVisual, "initialize", vis_init, 2);

  cColormap = rb_define_class_under(mX11, "Colormap", rb_cObject);
  rb_define_singleton_method(cColormap, "new", cmap_new, 0);
  rb_define_method(cColormap, "initialize", cmap_init, 0);

  cDrawable = rb_define_class_under(mX11, "Drawable", rb_cObject);
  rb_define_singleton_method(cDrawable, "new", drawable_new, 0);
  rb_define_method(cDrawable, "initialize", drawable_init, 0);

  cPixmap = rb_define_class_under(mX11, "Pixmap", rb_cObject);
  rb_define_singleton_method(cPixmap, "new", pmap_new, 0);
  rb_define_method(cPixmap, "initialize", pmap_init, 0);
#endif /* !X_DISPLAY_MISSING */

  /* fprintf(stderr, "DEBUG: Done Loading Imlib2.so.\n"); */
}

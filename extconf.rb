require 'mkmf'

imlib2_config = with_config("imlib2-config", "imlib2-config")

$CFLAGS << ' -DX_DISPLAY_MISSING ' << `#{imlib2_config} --cflags`.chomp
$LDFLAGS << ' ' << `#{imlib2_config} --libs`.chomp

if have_library("Imlib2", "imlib_create_image")
  # test for faulty versions of imlib2
  ver = `imlib2-config --version`.chomp.split(/\./)
  major, minor, revision = ver[0].to_i, ver[1].to_i, ver[2].to_i
  if ((major > 1)               ||
      (major == 1 && minor > 0) ||
      (major == 1 && minor == 0 && revision > 5))
    $CFLAGS << ' -DDISABLE_DRAW_PIXEL_WORKAROUND '
  else
    puts 'Note: This version of Imlib2 has a bug in imlib_image_draw_pixel().',
         'Enabling workaround (see documentation for details).'
  end

  create_makefile("imlib2")
end

#!/usr/bin/ruby

########################################################################
# test_font.rb - draw translucent text on an image                     #
# (c) 2002 Paul Duncan <pabs@pablotron.org>                            #
#                                                                      #
# - test Imlib2::Font methods and Imlib2::Color methods                #
########################################################################

# load imlib2 binding
require 'imlib2'

# text to draw
text = 'Bill Gates is a criminal!'

# filename variables
filename = 'images/bill_gates.jpg'
output_filename = 'test_font_1.png'
output_filename = ARGV.shift if ARGV.length != 0

# font variables
fontpath = 'fonts'
# these fonts were removed due to licensing issues
#fontname = 'quadapto/32'
#fontname = '20thcent/32'
fontname = 'yudit/32'

# font colors
colors = [ 
  Imlib2::Color::RED,
  Imlib2::Color::YELLOW,
  Imlib2::Color::GREEN,
  Imlib2::Color::AQUA,
  Imlib2::Color::BLUE,
  Imlib2::Color::VIOLET,
].each { |color| color.a = 128 }

# text drop-shadow x offset, y offset, and color
sh = {
  'x'     => 2,
  'y'     => 2,
  'color' => Imlib2::Color::RgbaColor.new(0, 0, 0, 64),
}

# load filename
begin
  im = Imlib2::Image::load filename
rescue Imlib2::FileError
  die "Couldn't load \"#{filename}\": " + $!
end

# add specified font path and load font
Imlib2::Font::add_path fontpath
font = Imlib2::Font.new fontname

# get the height and width of the specified text with our font, and use that
# to calculate the x and y offset so the text will be centered 
fw, fh = font.size text
fx, fy = (im.width - fw) / 2, (im.height / 2 - fh) / 2;

# draw text on image with specified font and color
c_i = -1
0.upto(im.height / fh) { |offset|
  # draw drop-shadow
  im.draw_text font, text, fx + sh['x'], fh * offset + sh['y'], sh['color']

  # draw text (in specified color)
  color = colors[c_i = (c_i + 1) % colors.length]
  im.draw_text font, text, fx, fh * offset, color
}

# save image to output filename
begin
  im.save output_filename
rescue Imlib2::FileError
  $stderr.puts "Couldn't save \"#{output_filename}\": " + $!
end

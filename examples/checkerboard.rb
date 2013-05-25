#!/usr/bin/ruby

########################################################################
# checkerboard.rb - draw a checkerboard                                #
# (c) 2002 Paul Duncan <pabs@pablotron.org>                            #
#                                                                      #
# - test Imlib2::Image#fill_rect, Imlib2::Image#save, and color        #
#   constants (Imlib2::Color::BLACK, Imlib2::Color::WHITE, etc)        #
########################################################################

# load imlib2 bindings
require "imlib2"

# number, width, and height of boxes
w, h, bw, bh = 10, 10, 48, 48

# foreground (gradient), background, top piece, and bottom piece colors
fg = [Imlib2::Color::DARKGREY, Imlib2::Color::LIGHTGREY]
bg = Imlib2::Color::BLACK
tc, bc = Imlib2::Color::RED, Imlib2::Color::BLUE

# set output filename (use command-line argument if specified)
filename = "checkerboard.png"
filename = ARGV.pop if ARGV.length > 0

# allocate the new image and fill it with the background color
im = Imlib2::Image.new w * bw, h * bh
im.fill_rect [0, 0], [w * bw, h * bh], bg

# create foreground gradient
gradient = Imlib2::Gradient.new [0, fg[0]], [1, fg[1]]

# fill squares with foreground gradient
puts "Generating checkerboard..."
0.upto(w / 2) { |x|
  0.upto(h / 2) { |y|
    (0..1).each { |i|
      rect = [x * bw * 2 + bw * i, y * bh * 2 + bh * i, bw, bh]
      im.fill_gradient gradient, rect, 135.0
    }
  }
}

# draw checker pieces
puts "Generating pieces..."
0.upto(w - 1) { |x|
  0.upto(2) { |y|
    # draw top piece
    rect = [x * bw + bw/2, y * bh + bh/2, bw * 2/5, bh * 2/5]
    # Imlib2 bug (works properly w/ draw_ellipse, but not w/ fill_ellipse)
    im.fill_ellipse rect, tc if (x - (y % 2)) % 2 == 1

    # draw bottom piece
    rect[1] = h * bh - rect[1]
    # Imlib2 bug (works properly w/ draw_ellipse, but not w/ fill_ellipse)
    im.fill_ellipse rect, bc if (x - (y % 2)) % 2 == 0
  }
}

# save image (with error checking)
puts "Saving to \"#{filename}\"."
begin
  im.save filename
rescue Imlib2::FileError
  $stderr.puts "Couldn't save \"#{filename}\": " + $!
end

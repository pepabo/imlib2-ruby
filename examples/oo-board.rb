#!/usr/bin/ruby

########################################################################
# oo-board.rb - draw a checkerboard (object-oriented version)          #
# (c) 2002 Paul Duncan <pabs@pablotron.org>                            #
#                                                                      #
# - test Imlib2::Image#fill_rect, Imlib2::Image#save, and color        #
#   constants (Imlib2::Color::BLACK, Imlib2::Color::WHITE, etc)        #
########################################################################

# load imlib2 bindings
require "imlib2"

class CheckerBoard
  attr_accessor :w, :h, :bw, :bh, :fg, :bg, :tc, :bc

  def initialize(*args)
    # number, width, and height of boxes
    @w, @h, @bw, @bh = args
    
    # foreground, background, top piece, and bottom piece colors
    @fg, @bg = Imlib2::Color::WHITE, Imlib2::Color::BLACK
    @tc, @bc = Imlib2::Color::RED, Imlib2::Color::BLUE
    
    # allocate the new image and fill it with the background color
    @im = Imlib2::Image.new @w * @bw, @h * @bh
    @im.fill_rect [0, 0], [@w * @bw, @h * @bh], @bg
  end

  # fill squares with foreground color
  def draw_squares()
    0.upto(@w / 2) { |x|
      0.upto(@h / 2) { |y|
        (0..1).each { |i|
          rect = [x * @bw * 2 + @bw * i, y * @bh * 2 + @bh * i, @bw, @bh]
          @im.fill_rect rect, @fg
        }
      }
    }
  end

  # draw checker pieces
  def draw_pieces()
    0.upto(@w - 1) { |x|
      0.upto(2) { |y|
        # draw top piece
        rect = [x * @bw + @bw / 2, y * @bh + @bh / 2, @bw * 0.4, @bh * 0.4]
        # Imlib2 bug (works w/ draw_ellipse, but not w/ fill_ellipse)
        @im.fill_ellipse rect, @tc if (x - (y % 2)) % 2 == 1
    
        # draw bottom piece
        rect[1] = h * @bh - rect[1]
        # Imlib2 bug (works w/ draw_ellipse, but not w/ fill_ellipse)
        @im.fill_ellipse rect, @bc if (x - (y % 2)) % 2 == 0
      }
    }
  end

  # save image
  def save(filename)
    @im.save filename
  end
end

# if this file is the executable, then test out our member functions by
# creating a CheckerBoard instance and saving it as 'checkerboard.png'
# (unless a filename was passed on the command line)
if $0 == __FILE__
  # set output filename (use argument if specified)
  filename = "checkerboard.png"
  filename = ARGV.pop if ARGV.length > 0

  board = CheckerBoard.new 10, 10, 48, 48
  
  puts "Generating checkerboard..."
  board.draw_squares

  puts "Generating pieces..."
  board.draw_pieces
  
  # save image (with error checking)
  puts "Saving image to \"#{filename}\"..."
  begin
    board.save filename
  rescue Imlib2::FileError
    $stderr.puts "Couldn't save \"#{filename}\": " + $!
  end

  puts "Done."
end

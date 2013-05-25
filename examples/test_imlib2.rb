#!/usr/bin/ruby

require 'imlib2'

puts 'Using Imlib2-Ruby version ' + Imlib2::VERSION + '.'

#####################################
# allocate some colors              #
# (test colors and color constants) #
#####################################
#black = Imlib2::Color::RgbaColor.new 0, 0, 0, 255
#white = Imlib2::Color::RgbaColor.new 255, 255, 255, 255
alpha = Imlib2::Color::RgbaColor.new 0, 0, 0, 128
black = Imlib2::Color::BLACK
white = Imlib2::Color::WHITE

##################################
# load an image                  #
# (test imlib2-style load_image) #
##################################
puts 'Testing Imlib2::Image::load_image'
im = Imlib2::Image.load_image 'images/bill_gates.jpg'
puts "im.width = #{im.width.to_s}\nim.height = #{im.height}"

##########################
# load a missing image   #
# (test ruby-style load) #
##########################
puts 'Testing Exceptions with Imlib2::Image::load ' + 
     '(there should be an error here)'
begin
  missing_image = Imlib2::Image.load 'alsjkfldfaljdfkaljdflkdf.gif'
rescue Imlib2::FileError
  $stderr.puts 'Error loading image: ' + $!
end

# test load with blocks
puts "Testing Imlib2::Image::load with blocks "  + 
     "(should print \"image loaded okay\",\n"    +
     "and not \"THIS SHOULDN'T BE PRINTED\")"
Imlib2::Image.load('images/clown.jpg') { |im| puts 'image loaded okay' }
Imlib2::Image.load('adklfj.png') { |im|
  $stderr.puts "THIS SHOULDN'T BE PRINTED"
}

####################################
# save test image                  #
# (test flip, fill_rect, and save) #
####################################
puts "Testing Imlib2::Image#save (shouldn't print any errors)"
im.flip_horizontal!
im.fill_rect [10, 10], [im.width - 20, im.height - 20], alpha
im.save 'test_output_1.jpg'

########################################
# make a checkerboard                  #
# (test fill_rect and color constants) #
########################################
puts "Creating a checkerboard...\n" +
     'Testing Imlib2::Image#fill_rect and Imlib2::Image#save'
w, h, bw, bh = 10, 10, 48, 48
bg, fg = Imlib2::Color::CYAN, Imlib2::Color::GREEN
im = Imlib2::Image.new w * bw, h * bh
im.fill_rect [0, 0], [640, 640], bg
0.upto(w / 2) { |x|
  0.upto(h / 2) { |y|
    (0..1).each { |i|
      rect = [x * bw * 2 + bw * i, y * bh * 2 + bh * i, bw, bh]
      im.fill_rect rect, fg
    }
  }
}
# test save
im.save 'test_output_2.png'

# test save with exceptions
puts 'Testing Imlib2::Image#save with exceptions (should print an error)'
begin
  im.save "./cant/save/here/because/it/doesnt/exist/checkerboard.png"
rescue Imlib2::FileError
  $stderr.puts 'Error saving image: ' + $!
end

puts 'Testing Imlib2::Image methods'
Imlib2::Image.load('images/clown.jpg'){ |im|
  puts 'Testing Imlib2::Image#blur'
  im2 = im.blur 5

  begin
    im2.save 'test_output_3.jpg'
  rescue Imlib2::FileError
    $stderr.puts 'Error saving image: ' + $!
    exit 1
  end

  puts 'Testing Imlib2::Image#sharpen!'
  im.sharpen! 5

  begin
    im.save 'test_output_4.jpg'
  rescue Imlib2::FileError
    $stderr.puts 'Error saving image: ' + $!
    exit 1
  end

  puts 'Testing Imlib2::Image#draw_pixel'
  0.upto(im.w) { |x| 
    y = im.h / 2 + im.h / 2 * Math::sin(1.0 * x / im.w * 2 * Math::PI - Math::PI)
    im.draw_pixel x, y, Imlib2::Color::GREEN
    im.draw_pixel x - 5, y, Imlib2::Color::BLUE
    im.draw_pixel x - 10, y, Imlib2::Color::RED
  }

  begin
    im.save 'test_output_5.jpg'
  rescue Imlib2::FileError
    $stderr.puts 'Error saving image: ' + $!
    exit 1
  end

}

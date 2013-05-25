#!/usr/bin/ruby

require 'imlib2'

filename = 'test_poly_1.png'

im = Imlib2::Image.new 320, 320
fg = Imlib2::Color::BLUE
bg = Imlib2::Gradient.new [0, Imlib2::Color::BLACK],
                          [1, Imlib2::Color::WHITE]

im.gradient bg, 0, 0, im.width, im.height, 135.0


points = []

0.upto(im.height / 10) { |y|
  points.push [5, y * im.height / 10], [im.width - 5, y * im.height / 10] 
}

poly = Imlib2::Polygon::new *points

im.fill_poly poly, fg

begin
  im.save filename
rescue Imlib2::FileError
  $stderr.puts "Couldn't save file \"#{filename}\": " + $!
end

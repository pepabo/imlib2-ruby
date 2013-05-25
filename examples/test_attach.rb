#!/usr/bin/env ruby

OUT_PATH = 'attach.jpg'

require 'imlib2'

# load image
im_path = ARGV.shift || (File::dirname($0) << '/images/bill_gates.jpg')
puts "Loading input image \"#{im_path}\"..."
im = Imlib2::Image.load(im_path)

# assign key, generate random number
key, val = (ARGV.shift || 'asdf'), (ARGV.shift || rand(10000)).to_i

puts "random number: #{val}"
puts "key: #{key}"
im[key] = val

puts "testing get_attach_value: #{im[key]}"

# save output image
puts 'Saving output image...'
im.save(OUT_PATH)

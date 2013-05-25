require 'rubygems'

spec = Gem::Specification.new do |s|

  #### Basic information.

  s.name = 'Imlib2-Ruby'
  s.version = '0.5.2'
  s.summary = <<-EOF
    Imlib2 bindings for Ruby.
  EOF
  s.description = <<-EOF
    Imlib2 bindings for Ruby.
  EOF

  s.requirements << 'Imlib2, version 1.2.0 (or newer)'
  s.requirements << 'Ruby, version 1.8.2 (or newer)'

  #### Which files are to be included in this gem?  Everything!  (Except CVS directories.)

  s.files = Dir.glob("**/*").delete_if { |item| item.include?("CVS") }

  #### C code extensions.

  s.require_path = 'lib' # is this correct?
  s.extensions << "extconf.rb"

  #### Load-time details: library and application (you will need one or both).
  s.autorequire = 'imlib2'
  s.has_rdoc = true
  s.rdoc_options = ['--title', 'Imlib2-Ruby API Documentation', '--webcvs', 'http://cvs.pablotron.org/cgi-bin/viewcvs.cgi/imlib2-ruby/', 'imlib2.c', 'README', 'ChangeLog', 'AUTHORS', 'COPYING', 'TODO']

  #### Author and project details.

  s.author = 'Paul Duncan'
  s.email = 'pabs@pablotron.org'
  s.homepage = 'http://www.pablotron.org/software/imlib2-ruby/'
  s.rubyforge_project = 'imlib2-ruby'
end

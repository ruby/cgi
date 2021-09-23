# frozen_string_literal: true

# Load built-in cgi/escape library
require 'cgi/escape.jar'
JRuby::Util.load_ext("org.jruby.ext.cgi.escape.CGIEscape")

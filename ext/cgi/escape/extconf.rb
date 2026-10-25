require 'mkmf'

if RUBY_ENGINE == 'truffleruby'
  File.write("Makefile", dummy_makefile($srcdir).join(""))
else
  if have_func(%[ruby_scan_digits()], "ruby/util.h")
    $defs.pop # defined but no declaration
  else
    have_func(%[ruby_scan_digits("", 0, 0, NULL, NULL)], "ruby/util.h")
  end
  have_var(%[ruby_hexdigits[0]], "ruby/util.h")
  have_var(%[ruby_digit36_to_number_table[0]], "ruby/util.h")
  create_makefile 'cgi/escape'
end

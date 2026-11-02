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

  # Same SIMD detection as ruby/erb's ext/erb/escape/extconf.rb.
  # Set CGI_DISABLE_SIMD to build the scalar path only.
  unless ENV["CGI_DISABLE_SIMD"]
    case RbConfig::CONFIG['host_cpu']
    when /^(arm64|aarch64)/
      # Try to compile a small program using NEON instructions
      header, type, init, extra = 'arm_neon.h', 'uint8x16_t', 'vdupq_n_u8(32)', nil
    when /^(x86_64|x64)/
      header, type, init, extra = 'x86intrin.h', '__m128i', '_mm_set1_epi8(32)', 'if (__builtin_cpu_supports("sse2")) { printf("OK"); }'
    end
    if header
      if have_header(header) && try_compile(<<~SRC, '-Werror=implicit-function-declaration')
          #{cpp_include(header)}
          int main(int argc, char **argv) {
            #{type} test = #{init};
            #{extra}
            if (argc > 100000) printf("%p", &test);
            return 0;
          }
        SRC
        $defs.push("-DCGI_ENABLE_SIMD")
      end
    end
  end

  create_makefile 'cgi/escape'
end

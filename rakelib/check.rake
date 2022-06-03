task :check do
  Bundler.with_unbundled_env do
    spec = Gem::Specification::load("cgi.gemspec")
    version = spec.version.to_s

    gem = "pkg/cgi-#{version}#{"-java" if RUBY_ENGINE == "jruby"}.gem"
    File.size?(gem) or abort "gem not built!"

    sh "gem", "install", gem

    require_relative "../test/lib/envutil"

    _, _, status = EnvUtil.invoke_ruby([], <<~EOS)
      version = #{version.dump}
      gem "cgi", version
      loaded_version = Gem.loaded_specs["cgi"].version.to_s
      if loaded_version == version
        puts "cgi \#{loaded_version} is loaded."
      else
        abort "cgi \#{loaded_version} is loaded instead of \#{version}!"
      end
      require "cgi/escape"

      string = "&<>"
      actual = CGI.escape(string)
      expected = "%26%3C%3E"
      puts "CGI.escape(\#{string.dump}) = \#{actual.dump}"
      if actual != expected
        abort "no! expected to be \#{expected.dump}!"
      end
    EOS

    if status.success?
      puts "check succeeded!"
    else
      warn "check failed!"
      exit status.exitstatus
    end
  end
end

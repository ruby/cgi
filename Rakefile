require "bundler/gem_tasks"
require "rake/testtask"

require 'rake/javaextensiontask'
Rake::JavaExtensionTask.new("escape") do |ext|
  ext.source_version = '1.8'
  ext.target_version = '1.8'
  ext.ext_dir = 'ext/java'
  ext.lib_dir = 'lib/cgi'

  task :build => :compile
end

unless RUBY_ENGINE == 'jruby'
  require 'rake/extensiontask'
  extask = Rake::ExtensionTask.new("cgi/escape") do |x|
    x.lib_dir.sub!(%r[(?=/|\z)], "/#{RUBY_VERSION}/#{x.platform}")
  end
end

Rake::TestTask.new(:test) do |t|
  t.libs << "test/lib"
  if RUBY_ENGINE == 'jruby'
    t.libs << "ext/java/org/jruby/ext/cgi/escape/lib"
  else
    t.libs << "lib/#{RUBY_VERSION}/#{extask.platform}"
  end
  t.ruby_opts << "-rhelper"
  t.test_files = FileList['test/**/test_*.rb']
end

task :default => :test
task :test => :compile

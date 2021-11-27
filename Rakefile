require "bundler/gem_tasks"
require "rake/testtask"

require 'rake/extensiontask'
extask = Rake::ExtensionTask.new("cgi/escape") do |x|
  x.lib_dir << "/#{RUBY_VERSION}/#{x.platform}"
end

Rake::TestTask.new(:test) do |t|
  t.libs << extask.lib_dir
  t.libs << "test/lib"
  t.ruby_opts << "-rhelper"
  t.test_files = FileList['test/**/test_*.rb']
end

task :default => :test

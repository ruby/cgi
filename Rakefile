require "bundler/gem_tasks"
require "rake/testtask"

require 'rake/extensiontask'
extask = Rake::ExtensionTask.new("cgi/escape") do |x|
  x.lib_dir.sub!(%r[(?=/|\z)], "/#{RUBY_VERSION}/#{x.platform}")
end

Rake::TestTask.new(:test) do |t|
  t.libs << "lib/#{RUBY_VERSION}/#{extask.platform}"
  t.libs << "test/lib"
  t.ruby_opts << "-rhelper"
  t.test_files = FileList['test/**/test_*.rb']
end

task :default => :test
task :test => :compile

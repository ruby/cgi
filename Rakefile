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

task :sync_tool do
  require 'fileutils'
  FileUtils.cp "../ruby/tool/lib/core_assertions.rb", "./test/lib"
  FileUtils.cp "../ruby/tool/lib/envutil.rb", "./test/lib"
  FileUtils.cp "../ruby/tool/lib/find_executable.rb", "./test/lib"
end

task :default => :test

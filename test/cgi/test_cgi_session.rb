# frozen_string_literal: true
require 'test/unit'
require 'cgi'
require 'cgi/session'
require 'cgi/session/pstore'
require 'stringio'
require 'tmpdir'
require_relative 'update_env'

class CGISessionTest < Test::Unit::TestCase
  include UpdateEnv

  def setup
    @environ = {}
    @session_dir = Dir.mktmpdir(%w'session dir')
  end

  def teardown
    ENV.update(@environ)
    $stdout = STDOUT
    FileUtils.rm_rf(@session_dir)
  end

  def test_cgi_session_filestore
    update_env(
      'REQUEST_METHOD'  => 'GET',
  #    'QUERY_STRING'    => 'id=123&id=456&id=&str=%40h+%3D%7E+%2F%5E%24%2F',
  #    'HTTP_COOKIE'     => '_session_id=12345; name1=val1&val2;',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )
    value1="value1"
    value2="\x8F\xBC\x8D]".dup
    value2.force_encoding("SJIS") if defined?(::Encoding)
    cgi = CGI.new
    session = CGI::Session.new(cgi,"tmpdir"=>@session_dir)
    session["key1"]=value1
    session["key2"]=value2
    assert_equal(value1,session["key1"])
    assert_equal(value2,session["key2"])
    session.close
    $stdout = StringIO.new
    cgi.out{""}

    update_env(
      'REQUEST_METHOD'  => 'GET',
      # 'HTTP_COOKIE'     => "_session_id=#{session_id}",
      'QUERY_STRING'    => "_session_id=#{session.session_id}",
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )
    cgi = CGI.new
    session = CGI::Session.new(cgi,"tmpdir"=>@session_dir)
    $stdout = StringIO.new
    assert_equal(value1,session["key1"])
    assert_equal(value2,session["key2"])
    session.close
  end

  def test_cgi_session_pstore
    update_env(
      'REQUEST_METHOD'  => 'GET',
  #    'QUERY_STRING'    => 'id=123&id=456&id=&str=%40h+%3D%7E+%2F%5E%24%2F',
  #    'HTTP_COOKIE'     => '_session_id=12345; name1=val1&val2;',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )
    value1="value1"
    value2="\x8F\xBC\x8D]".dup
    value2.force_encoding("SJIS") if defined?(::Encoding)
    cgi = CGI.new
    session = CGI::Session.new(cgi,"tmpdir"=>@session_dir,"database_manager"=>CGI::Session::PStore)
    session["key1"]=value1
    session["key2"]=value2
    assert_equal(value1,session["key1"])
    assert_equal(value2,session["key2"])
    session.close
    $stdout = StringIO.new
    cgi.out{""}

    update_env(
      'REQUEST_METHOD'  => 'GET',
      # 'HTTP_COOKIE'     => "_session_id=#{session_id}",
      'QUERY_STRING'    => "_session_id=#{session.session_id}",
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )
    cgi = CGI.new
    session = CGI::Session.new(cgi,"tmpdir"=>@session_dir,"database_manager"=>CGI::Session::PStore)
    $stdout = StringIO.new
    assert_equal(value1,session["key1"])
    assert_equal(value2,session["key2"])
    session.close
  end if defined?(::PStore)

  def test_cgi_session_specify_session_id
    update_env(
      'REQUEST_METHOD'  => 'GET',
  #    'QUERY_STRING'    => 'id=123&id=456&id=&str=%40h+%3D%7E+%2F%5E%24%2F',
  #    'HTTP_COOKIE'     => '_session_id=12345; name1=val1&val2;',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )
    value1="value1"
    value2="\x8F\xBC\x8D]".dup
    value2.force_encoding("SJIS") if defined?(::Encoding)
    cgi = CGI.new
    session = CGI::Session.new(cgi,"tmpdir"=>@session_dir,"session_id"=>"foo")
    session["key1"]=value1
    session["key2"]=value2
    assert_equal(value1,session["key1"])
    assert_equal(value2,session["key2"])
    assert_equal("foo",session.session_id)
    #session_id=session.session_id
    session.close
    $stdout = StringIO.new
    cgi.out{""}

    update_env(
      'REQUEST_METHOD'  => 'GET',
      # 'HTTP_COOKIE'     => "_session_id=#{session_id}",
      'QUERY_STRING'    => "_session_id=#{session.session_id}",
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )
    cgi = CGI.new
    session = CGI::Session.new(cgi,"tmpdir"=>@session_dir)
    $stdout = StringIO.new
    assert_equal(value1,session["key1"])
    assert_equal(value2,session["key2"])
    assert_equal("foo",session.session_id)
    session.close
  end

  def test_cgi_session_specify_session_key
    update_env(
      'REQUEST_METHOD'  => 'GET',
  #    'QUERY_STRING'    => 'id=123&id=456&id=&str=%40h+%3D%7E+%2F%5E%24%2F',
  #    'HTTP_COOKIE'     => '_session_id=12345; name1=val1&val2;',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )
    value1="value1"
    value2="\x8F\xBC\x8D]".dup
    value2.force_encoding("SJIS") if defined?(::Encoding)
    cgi = CGI.new
    session = CGI::Session.new(cgi,"tmpdir"=>@session_dir,"session_key"=>"bar")
    session["key1"]=value1
    session["key2"]=value2
    assert_equal(value1,session["key1"])
    assert_equal(value2,session["key2"])
    session_id=session.session_id
    session.close
    $stdout = StringIO.new
    cgi.out{""}

    update_env(
      'REQUEST_METHOD'  => 'GET',
      'HTTP_COOKIE'     => "bar=#{session_id}",
      # 'QUERY_STRING'    => "bar=#{session.session_id}",
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )
    cgi = CGI.new
    session = CGI::Session.new(cgi,"tmpdir"=>@session_dir,"session_key"=>"bar")
    $stdout = StringIO.new
    assert_equal(value1,session["key1"])
    assert_equal(value2,session["key2"])
    session.close
  end

  def test_cgi_session_filestore_path
    session_id = "banana"
    path_default = session_file_store_path("tmpdir"=>@session_dir, "session_id"=>session_id)
    assert_session_filestore_path(path_default)
    assert_equal path_default, session_file_store_path("tmpdir"=>@session_dir, "session_id"=>session_id)
    path_sha512 = session_file_store_path("tmpdir"=>@session_dir, "session_id"=>session_id, "digest"=>"SHA512")
    assert_session_filestore_path(path_default)
    assert_not_equal path_sha512, path_default
    path_long = session_file_store_path("tmpdir"=>@session_dir, "session_id"=>session_id, "length"=>32)
    assert_equal path_default.length+16, path_long.length
    assert_send [path_long, :start_with?, path_default]

    path = session_file_store_path("tmpdir"=>@session_dir, "session_id"=>session_id, "digest"=>Digest::SHA256)
    assert_equal path_default, path
    path = assert_session_filestore_path(session_file_store_path("tmpdir"=>@session_dir, "session_id"=>session_id, "digest"=>Digest::SHA512))
    assert_equal path_sha512, path
  end

  def test_rejects_insecure_session_file
    session_id = "insecure"
    path = session_file_store_path("tmpdir"=>@session_dir,
                                   "session_id"=>session_id)
    File.write(path, "")
    File.chmod(0644, path)

    assert_raise(CGI::Session::UnsafeSessionFileError) do
      CGI::Session.new(Object.new, "tmpdir"=>@session_dir,
                                   "session_id"=>session_id)
    end
  end unless CGI::Session.const_get(:MODE_MASK).zero?

  def test_filestore_update_rejects_existing_new_file
    session = CGI::Session.new(Object.new, "tmpdir"=>@session_dir,
                                           "session_id"=>"stale")
    path = session.instance_variable_get(:@dbman).instance_variable_get(:@path)
    new_path = path + ".new"
    File.write(new_path, "existing")
    session["key"] = "value"

    assert_raise(Errno::EEXIST) do
      session.close
    end
    assert_equal("existing", File.read(new_path))
  ensure
    session.delete if session
  end

  def test_pstore_does_not_enable_thread_safety_for_compatibility
    session = CGI::Session.new(Object.new, "tmpdir"=>@session_dir,
                                           "session_id"=>"pstore-compat",
                                           "database_manager"=>CGI::Session::PStore)
    pstore = session.instance_variable_get(:@dbman).instance_variable_get(:@p)

    assert_equal(false, pstore.instance_variable_get(:@thread_safe))
  ensure
    session.delete if session
  end if defined?(::PStore)

  def test_pstore_rejects_session_file_replaced_by_symlink
    omit("O_NOFOLLOW is not supported") unless nofollow_supported?

    session = CGI::Session.new(Object.new, "tmpdir"=>@session_dir,
                                           "session_id"=>"pstore-symlink",
                                           "database_manager"=>CGI::Session::PStore)
    session["key"] = "secret"
    pstore = session.instance_variable_get(:@dbman).instance_variable_get(:@p)
    target = File.join(@session_dir, "target")
    File.write(target, "")
    File.unlink(pstore.path)
    File.symlink("target", pstore.path)

    assert_raise(Errno::ELOOP) do
      session.close
    end
    assert_empty(File.read(target))
  ensure
    session.delete if session
  end if defined?(::PStore) and !CGI::Session::PStore::PSTORE_OPT.empty?

  private

  def nofollow_supported?
    return false unless File.const_defined?(:NOFOLLOW)

    target = File.join(@session_dir, "nofollow-target")
    link = File.join(@session_dir, "nofollow-link")
    File.write(target, "")
    File.symlink(target, link)
    File.open(link, File::RDONLY|File::NOFOLLOW).close
    false
  rescue Errno::ELOOP
    true
  rescue NotImplementedError, SystemCallError
    false
  ensure
    [target, link].each {|file| File.unlink(file) if file}
  end

  def assert_session_filestore_path(path, dir: @session_dir, prefix: "cgi_sid_", suffix: nil)
    base = File.basename(path)
    assert_equal dir, File.dirname(path)
    assert_operator base, :start_with?, prefix if prefix
    assert_operator base, :end_with?, suffix if suffix
    path
  end

  def session_file_store_path(options)
    cgi = Object.new
    session = CGI::Session.new(cgi, options)
    session.delete
    dbman = session.instance_variable_get(:@dbman)
    assert_kind_of(CGI::Session::FileStore, dbman)
    dbman.instance_variable_get(:@path)
  end
end

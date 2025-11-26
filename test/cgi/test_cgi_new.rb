# frozen_string_literal: true
require 'test/unit'
require 'cgi'
require 'stringio'
require_relative 'update_env'

# Test suite for CGI.new method functionality and documentation validation.
# Ensures the enhanced documentation matches actual implementation behavior.
class CGINewTest < Test::Unit::TestCase
  include UpdateEnv

  def setup
    @environ = {}
    @original_stdin = $stdin
  end

  def teardown
    ENV.update(@environ)
    $stdin = @original_stdin
  end

  # Test basic CGI object creation with all documented call sequences
  def test_basic_object_creation
    update_env(
      'REQUEST_METHOD' => 'GET',
      'QUERY_STRING' => '',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )

    # CGI.new(options = {}) -> new_cgi
    cgi1 = CGI.new({})
    assert_instance_of(CGI, cgi1)

    # CGI.new(tag_maker) -> new_cgi
    cgi2 = CGI.new('html5')
    assert_instance_of(CGI, cgi2)

    # With blocks - should not raise errors
    assert_nothing_raised { CGI.new({}) { |name, value| } }
    assert_nothing_raised { CGI.new('html5') { |name, value| } }
  end

  # Test tag_maker functionality for all documented HTML versions
  def test_tag_maker_functionality
    update_env(
      'REQUEST_METHOD' => 'GET',
      'QUERY_STRING' => '',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )

    html_versions = {
      'html3' => '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2 Final//EN">',
      'html4' => '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">',
      'html4Tr' => '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">',
      'html4Fr' => '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Frameset//EN" "http://www.w3.org/TR/html4/frameset.dtd">',
      'html5' => '<!DOCTYPE HTML>'
    }

    html_versions.each do |version, expected_doctype|
      cgi = CGI.new(tag_maker: version)
      assert_respond_to(cgi, :doctype, "HTML generation methods should be loaded for #{version}")
      assert_equal(expected_doctype, cgi.doctype)
    end

    # Test that without tag_maker, HTML methods are not loaded
    cgi = CGI.new
    assert_raise(NoMethodError) { cgi.doctype }
  end

  # Test string tag_maker argument equivalence to hash option
  def test_string_tag_maker_equivalent
    update_env(
      'REQUEST_METHOD' => 'GET',
      'QUERY_STRING' => '',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )

    cgi1 = CGI.new('html5')
    cgi2 = CGI.new(tag_maker: 'html5')

    # Both should have HTML generation methods loaded
    assert_respond_to(cgi1, :doctype)
    assert_respond_to(cgi2, :doctype)

    # Both should produce the same doctype
    assert_equal(cgi1.doctype, cgi2.doctype)
    assert_equal('<!DOCTYPE HTML>', cgi1.doctype)
  end

  # Test offline mode (when REQUEST_METHOD is not defined)
  def test_offline_mode
    ENV.delete('REQUEST_METHOD')
    ENV.delete('QUERY_STRING')
    ENV.delete('SERVER_SOFTWARE')
    ENV.delete('SERVER_PROTOCOL')

    # Create test input
    test_input = "name=value&test=123"
    $stdin = StringIO.new(test_input)

    cgi = CGI.new

    # In offline mode, it should read from stdin
    assert_equal("value", cgi['name'])
    assert_equal("123", cgi['test'])
  end

  # Test that all documented options are accepted without errors
  def test_options_acceptance
    update_env(
      'REQUEST_METHOD' => 'GET',
      'QUERY_STRING' => '',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )

    # Test accept_charset option
    assert_nothing_raised { CGI.new(accept_charset: 'EUC-JP') }
    assert_nothing_raised { CGI.new(accept_charset: Encoding::UTF_8) }

    # Test max_multipart_length options
    assert_nothing_raised { CGI.new(max_multipart_length: 1024 * 1024) }
    assert_nothing_raised { CGI.new(max_multipart_length: -> { 2 * 1024 * 1024 }) }

    # Test combined options
    assert_nothing_raised do
      CGI.new(
        accept_charset: 'ISO-8859-1',
        max_multipart_length: 64 * 1024 * 1024,
        tag_maker: 'html5'
      )
    end
  end

  # Test basic object structure and public methods
  def test_object_structure
    update_env(
      'REQUEST_METHOD' => 'GET',
      'QUERY_STRING' => 'foo=bar',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )

    cgi = CGI.new

    # Test documented instance variables and methods exist
    assert_kind_of(Hash, cgi.cookies)
    assert_kind_of(Hash, cgi.params)
    assert_equal(false, cgi.multipart?)
    assert_equal("bar", cgi['foo'])  # Verify param parsing works
  end

  # Test accept_charset method behavior (HTTP header vs configuration)
  def test_accept_charset_method_behavior
    update_env(
      'REQUEST_METHOD' => 'GET',
      'QUERY_STRING' => '',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )

    # Test without HTTP_ACCEPT_CHARSET header - method should return nil
    cgi1 = CGI.new(accept_charset: 'EUC-JP')
    assert_nil(cgi1.accept_charset, "accept_charset method should return HTTP header, not config")
    assert_equal('EUC-JP', cgi1.instance_variable_get(:@accept_charset))

    # Test with HTTP_ACCEPT_CHARSET header - method should return header value
    update_env('HTTP_ACCEPT_CHARSET' => 'ISO-8859-1')
    cgi2 = CGI.new(accept_charset: 'UTF-8')
    assert_equal('ISO-8859-1', cgi2.accept_charset, "accept_charset method should return HTTP header")
    assert_equal('UTF-8', cgi2.instance_variable_get(:@accept_charset))
  end

  # Test encoding error block handling
  def test_encoding_error_block_handling
    # Test that a block can be provided (even if encoding errors don't occur in this simple case)
    test_input = "name=value"
    update_env(
      'REQUEST_METHOD' => 'POST',
      'CONTENT_TYPE' => 'application/x-www-form-urlencoded',
      'CONTENT_LENGTH' => test_input.length.to_s,
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )

    $stdin = StringIO.new(test_input)

    encoding_errors = {}
    assert_nothing_raised do
      cgi = CGI.new(accept_charset: 'UTF-8') do |name, value|
        encoding_errors[name] = value
      end
      assert_equal("value", cgi['name'])
    end
  end

  # Test class vs instance charset behavior
  def test_class_vs_instance_charset
    update_env(
      'REQUEST_METHOD' => 'GET',
      'QUERY_STRING' => '',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )

    # Class default should be UTF-8
    assert_equal(Encoding::UTF_8, CGI.accept_charset)

    # Instance with no option should use class default internally
    cgi = CGI.new
    assert_equal(Encoding::UTF_8, cgi.instance_variable_get(:@accept_charset))
  end

  # Test max_multipart_length configuration (no public getter available)
  def test_max_multipart_length_configuration
    update_env(
      'REQUEST_METHOD' => 'GET',
      'QUERY_STRING' => '',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )

    # Test with integer value - should not raise error
    custom_size = 1024 * 1024 # 1 MB
    cgi = CGI.new(max_multipart_length: custom_size)
    assert_equal(custom_size, cgi.instance_variable_get(:@max_multipart_length))

    # Test with lambda - should not raise error
    check_lambda = -> { 2 * 1024 * 1024 } # 2 MB
    cgi = CGI.new(max_multipart_length: check_lambda)
    assert_equal(check_lambda, cgi.instance_variable_get(:@max_multipart_length))
  end

  # Test that configuration options don't interfere with each other
  def test_option_assignment
    update_env(
      'REQUEST_METHOD' => 'GET',
      'QUERY_STRING' => '',
      'SERVER_SOFTWARE' => 'Apache 2.2.0',
      'SERVER_PROTOCOL' => 'HTTP/1.1',
    )

    # Create CGI instances with different combinations of options
    cgi1 = CGI.new(accept_charset: 'EUC-JP')
    cgi2 = CGI.new(max_multipart_length: 512 * 1024)
    cgi3 = CGI.new(tag_maker: 'html4')
    cgi4 = CGI.new(
      accept_charset: 'ISO-8859-1',
      max_multipart_length: 256 * 1024,
      tag_maker: 'html5'
    )

    # Verify each has the expected configuration
    assert_equal('EUC-JP', cgi1.instance_variable_get(:@accept_charset))
    assert_equal(512 * 1024, cgi2.instance_variable_get(:@max_multipart_length))
    assert_respond_to(cgi3, :doctype)

    assert_equal('ISO-8859-1', cgi4.instance_variable_get(:@accept_charset))
    assert_equal(256 * 1024, cgi4.instance_variable_get(:@max_multipart_length))
    assert_respond_to(cgi4, :doctype)
    assert_equal('<!DOCTYPE HTML>', cgi4.doctype)
  end
end

require "test_helper"

class CgiTest < Minitest::Test
  def test_that_it_has_a_version_number
    refute_nil ::Cgi::VERSION
  end

  def test_it_does_something_useful
    assert false
  end
end

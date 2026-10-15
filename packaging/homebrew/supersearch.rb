class Supersearch < Formula
  desc "Ultra-fast parallel code & symbol search engine in pure C (AVX2 & ARM NEON) with MCP 2.0 server"
  homepage "https://github.com/aliemreerdem/super-search"
  version "2.6.0"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/aliemreerdem/super-search/releases/download/v2.6.0/supersearch-macos-arm64.tar.gz"
      sha256 "68876e1ce5ffb751f1678872f9720525c7e84f8db9864bedf37ffcd8c5da1a6a"
    else
      url "https://github.com/aliemreerdem/super-search/releases/download/v2.6.0/supersearch-macos-x64.tar.gz"
      sha256 "bef30772c2996e372020449eca5cbcc9f80d82555ca2d82168cf92c94e810b6d"
    end
  end

  on_linux do
    if Hardware::CPU.arm?
      url "https://github.com/aliemreerdem/super-search/releases/download/v2.6.0/supersearch-linux-arm64.tar.gz"
      sha256 "98dc485187c84d619f9a49624779d556dbd43762ae57107be3117468a442bb91"
    else
      url "https://github.com/aliemreerdem/super-search/releases/download/v2.6.0/supersearch-linux-x64.tar.gz"
      sha256 "95ae7911418768010ca9581de0ab31dc9af1c69d433e3f2a9b3ec3e246c4023a"
    end
  end

  def install
    bin.install "bin/supersearch"
    pkgshare.install ".agents/skills/supersearch"
  end

  test do
    assert_match "SuperSearch", shell_output("#{bin}/supersearch --help")
  end
end

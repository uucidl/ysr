#!/usr/bin/ruby
#
# This script is used to build an audio unit / component bundle from a plain executable.
#

require 'fileutils'

INFO_PLIST = %q{<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
        <key>CFBundleDevelopmentRegion</key>
        <string>English</string>
        <key>CFBundleExecutable</key>
        <string>#{@executable}</string>
        <key>CFBundleName</key>
        <string>#{@bundle_name}</string>
        <key>CFBundleIdentifier</key>
        <string>#{@identifier}</string>
        <key>CFBundleInfoDictionaryVersion</key>
        <string>6.0</string>
        <key>CFBundlePackageType</key>
        <string>BNDL</string>
        <key>CFBundleShortVersionString</key>
        <string>#{@short_info}</string>
        <key>CFBundleSignature</key>
        <string>#{@signature}</string>
        <key>CFBundleVersion</key>
        <string>#{@version}</string>
        <key>CSResourcesFileMapped</key>
        <true/>
</dict>
</plist>}

PKGINFO= %q{BNDL#{@signature}}

##
# evaluate the content string so that substitution
# can take place

def produce(content)
  string = ""
  content.gsub!('"') { |a|
    '\"'
  }
  begin
    eval("string = \"#{content}\"")
  rescue
    print("in #{@name}, an error: #{$!}\n")
  end
  return string
end

if ARGV.length < 2
  puts "usage: make-library-bundle.rb <package> <executable> <data-files | icon-file...>"
  exit
end

PACKAGE_NAME = ARGV[0]
EXECUTABLE_PATH = File.expand_path(ARGV[1])
BUNDLE_EXT = ARGV[2]

def die(msg)
  print "#{msg}\n"
  exit 1
end

if File.exists? EXECUTABLE_PATH
  @executable = File.basename(EXECUTABLE_PATH)
  BUNDLE_NAME = File.basename(EXECUTABLE_PATH, File.extname(EXECUTABLE_PATH))
  BUNDLE_PATH = File.dirname(EXECUTABLE_PATH) + "/" + BUNDLE_NAME + BUNDLE_EXT

  @executable_path = "#{BUNDLE_PATH}/Contents/MacOS/#{@executable}";

  if File.directory? BUNDLE_PATH then
    FileUtils.remove_dir("#{BUNDLE_PATH}")
  end

  FileUtils.makedirs("#{BUNDLE_PATH}/Contents/MacOS/")
  FileUtils.install(EXECUTABLE_PATH, @executable_path, :mode => 0755)
  FileUtils.makedirs("#{BUNDLE_PATH}/Contents/Resources/")
  FileUtils.makedirs("#{BUNDLE_PATH}/Contents/Libraries/")
  (2 ... ARGV.length).each {|i|
    file = ARGV[i]

    if File.directory? file or File.exists? file then
    if file =~ /\.dylib$/ then
      dest = "#{BUNDLE_PATH}/Contents/Libraries/"
      libname = File.basename(file)
      system ("install_name_tool -change #{file} @executable_path/../Libraries/#{libname} #{@executable_path}");
    else
      dest = "#{BUNDLE_PATH}/Contents/Resources/"
    end

    FileUtils.cp_r(file, dest)
    if /\.icns$/ =~ file
      @icon = File.basename(file)
    end
    end
  }

  @identifier = PACKAGE_NAME.gsub('/', '.')
  @bundle_name = BUNDLE_NAME
  @signature = "????"
  @version = "1.0"
  @short_info = "1.0"

  info_plist = File.open("#{BUNDLE_PATH}/Contents/Info.plist", "w")
  info_plist.write(produce(INFO_PLIST))
  info_plist.close

  pkginfo = File.open("#{BUNDLE_PATH}/Contents/PkgInfo", "w")
  pkginfo.write(produce(PKGINFO))
  pkginfo.close
else
  raise "File not found: #{EXECUTABLE_PATH}"
end

=begin

Create a MACOSX library bundle.

=end

#!/usr/bin/ruby
#
# This script is used to build an IOS application bundle from a plain executable.
#

require 'ftools'
require 'fileutils'

INFO_PLIST = %q{<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleExecutable</key>
	<string>#{@executable}</string>
	<key>CFBundleGetInfoString</key>
	<string>#{@info}</string>
	<key>CFBundleIconFile</key>
	<string>#{@icon}</string>
	<key>CFBundleIdentifier</key>
	<string>#{@identifier}</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleDisplayName</key>
	<string>#{@name}</string>
	<key>CFBundleName</key>
	<string>#{@name}</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleShortVersionString</key>
	<string>#{@short_info}</string>
	<key>CFBundleSignature</key>
	<string>#{@signature}</string>
	<key>CFBundleVersion</key>
	<string>#{@version}</string>
	<key>NSMainNibFile</key>
	<string>#{@nib_file}</string>
	<key>LSRequiresIPhoneOS</key>
	<true/>
</dict>
</plist>}

@nib_file = 'MainWindow'
#@icon = 'Icon-Small.png'

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
  puts "usage: make-application-bundle.rb <package> <executable> <data-files | icon-file...>"
  exit
end

PACKAGE_NAME = ARGV[0]
EXECUTABLE_PATH = File.expand_path(ARGV[1])
DEST = File.expand_path("#{File.dirname($0)}/../../OUTPUT")

def die(msg) 
  print "#{msg}\n"
  exit 1
end

if File.exists? EXECUTABLE_PATH
  @executable = File.basename(EXECUTABLE_PATH)
  BUNDLE_NAME = File.basename(EXECUTABLE_PATH, '.elf')
  BUNDLE_PATH = File.dirname(EXECUTABLE_PATH) + "/" + BUNDLE_NAME + ".app"
  
  @name = BUNDLE_NAME

  @executable_path = "#{BUNDLE_PATH}/#{@executable}";

  if File.directory? BUNDLE_PATH then
    FileUtils.remove_dir("#{BUNDLE_PATH}", true)
  end

  File.makedirs("#{BUNDLE_PATH}")
  File.install(EXECUTABLE_PATH, @executable_path, 0755)
  (2 ... ARGV.length).each {|i|
    file = ARGV[i]

    if File.directory? file or File.exists? file then
	dest = "#{BUNDLE_PATH}/"

      FileUtils.cp_r(file, dest)
      if /\.icns$/ =~ file
	@icon = File.basename(file)
      end
    else
      print "Could not find #{file}\n"
    end
  }
  
  @identifier = PACKAGE_NAME.gsub('/', '.') + "." + BUNDLE_NAME
  @bundle_name = BUNDLE_NAME
  @signature = BUNDLE_NAME
  
  info_plist = File.open("#{BUNDLE_PATH}/Info.plist", "w")
  info_plist.write(produce(INFO_PLIST))
  info_plist.close   
else
  raise "File not found: #{EXECUTABLE_PATH}"
end

=begin

Create a MACOSX application bundle.

=end

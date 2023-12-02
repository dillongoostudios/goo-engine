import sys
import os
from os import path
import subprocess

print("Checkout submodules")
subprocess.check_call(['git','submodule','update','--init','--recursive'])

print("Get binary dependencies")
if sys.platform.startswith('linux'):
    #Assumes apt-get
    subprocess.check_call(['sudo','apt-get','update'])
    subprocess.check_call(['sudo','apt-get','install','build-essential',
    'git','subversion','cmake','libx11-dev','libxxf86vm-dev','libxcursor-dev',
    'libxi-dev','libxrandr-dev','libxinerama-dev','libglew-dev'])

SVN_DIR= None
platform_dir = {
    'win32' : 'win64_vc15',
    'darwin' : 'darwin',
    'linux' : 'linux_centos7_x86_64'
}
for key, value in platform_dir.items():
    if sys.platform.startswith(key):
        SVN_DIR = value

SVN = 'https://svn.blender.org/svnroot/bf-blender/tags/blender-4.0-release/lib/' + SVN_DIR
binaries_path = path.join('..','lib',SVN_DIR)
try:
    subprocess.check_call(['svn','checkout',SVN,binaries_path])
except:
    subprocess.check_call(['svn','switch',SVN,binaries_path])

BUILD_DIR = path.join('..','build_release')

print("Make sure the build directory exists")
if path.exists(BUILD_DIR) == False:
    os.mkdir(BUILD_DIR)

print("Configure Blender project")
if sys.platform == 'win32':
    #Specify 64 bits architecture on Windows
    subprocess.check_call(['cmake','-A','x64','-S','.','-B',BUILD_DIR])
else:
    subprocess.check_call(['cmake','-S','.','-B',BUILD_DIR])

print("Build Blender project")
subprocess.check_call(['cmake','--build',BUILD_DIR,'--target','install','--config','Release'])

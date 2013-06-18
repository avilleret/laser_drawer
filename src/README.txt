ILDA is a library for puredata to control ILDA laser projector with any sound card (you may need to remove some DC filters)

There are three externals in this library : 
ildasend : send table values and projection setting through OSC
ildareceive : receive OSC data, decode it, update tables and so on...
ildafile : decode .ild file according to ILDA file specification and put frames in tables

TODO:
- add support to all settings (mainly angle correction and end line correction)
- improve data transmission
- make it works on RPi

Dependencies :
[ildasend] and [ildareceive] depends on liblo
[ildareceive] also depends on OpenCV to correct perspective, but it could be build and it works without (only the perspective correction function is not available)
01-ILDA-display_vector.pd example depends on latest Gem (it needs [gemvertexbuffer])

Building :
it should build with the default pd build system if liblo is installed :

make
sudo make install

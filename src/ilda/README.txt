laser_driver is an external object for puredata to control ILDA laser projector.

It receives raw udp data from udpreceive and treats them according to some settings.

Here is the structure of a frame :

"StartNewTrame" 
<4 byte (MSB first) : Frame length in bytes> 
< 1 byte : 0 | 1 wether this is a settings frame (0) or a data frame (1)>

for a settings frame :
< 2 bytes (MSB first) : x_offet >
< 2 bytes (MSB first) : y_offet >
< 2 bytes (MSB first) : x_scale >
< 2 bytes (MSB first) : y_scale >
< 1 byte 0 | 1 : invert x >
< 1 byte 0 | 1 : invert y >
< 1 byte 0 | 1 : turn off blanking >
< 1 byte 0 | 1 : invert blancking >
< 1 byte : intensity >
< 1 byte : color mode (0:analog color, 1:digital color, 2:analog green, 3:digital green)
< 2 bytes (MSB first) : angle correction (wait few µs on each corner) >
< 2 bytes (MSB first) : end line correction (wait few µs on each line end) >
< 2 bytes (MSB first) : scanner frequency >

for a data frame :
< 2 bytes (MSB first) : number of line strip >

for each line strip :
< 1 byte : red >
< 1 byte : green >
< 1 byte : blue >
< 2 bytes (MSB first) : number of points >

for each point :
< 2 bytes (MSB first) : x >
< 2 bytes (MSB first) : y >


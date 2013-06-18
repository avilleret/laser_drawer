#~ this script starts pd
#~ some args should be tuned depending on your system
#~ this have been tested on a Raspberry Pi
pd -noprefs -rt -alsa -noadc -nomidi -audiooutdev 3 -outchannels "6" -blocksize 2048 -audiobuf 100 ilda_server.pd

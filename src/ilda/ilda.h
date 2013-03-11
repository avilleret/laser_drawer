#include <limits.h>
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <netinet/in.h>
#endif /* _WIN32 */

#define PT_COORD_MAX 32767.5
#define ILDA_CH_X 0
#define ILDA_CH_Y 1
//~ #define ILDA_CH_Z 1
#define ILDA_CH_R 2
#define ILDA_CH_G 3
#define ILDA_CH_B 4
#define ILDA_HEADER_SIZE 9
#define ILDA_SETTING_SIZE 37


/* The int4byte type has to be a 4-byte integer.  You may have to
   change this to long or something else on your system.  */
typedef int int4byte;

typedef struct ilda_settings
{
    float scale[3], offset[3], invert[3]; //~ scale, offset and invert for X,Y and intensity
    float intensity; //~ global intensity value
    int mode; //~ 0:analog color, 1:digital color, 2:analog green, 3:digital green
    int blanking_off;
    float angle_correction, end_line_correction, scan_freq;
    
} t_ilda_settings;

typedef struct ilda_channel
{
    t_garray *array;
    t_symbol *arrayname;
    t_symbol *channelname;
    t_word  *vec;
    int vecsize;
} t_ilda_channel;

typedef struct t_ilda_frame
{
    int format;
    t_symbol *frame_name;
    t_symbol *compagny_name;
    unsigned int point_number;
    unsigned int frame_number;
    unsigned int total_frame;
    char scanner;
    unsigned int start_id;
} t_ilda_frame;

typedef struct t_ilda_colorlookuptable
{
    int format;

} t_ilda_colorlookuptable;

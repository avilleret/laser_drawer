typedef struct ilda_settings
{
    float scale_x, scale_y, offset_x, offset_y;
    int intensity;
    int mode; //~ 0:analog color, 1:digital color, 2:analog green, 3:digital green
    int invert_x, invert_y, blanking_off, invert_blancking;
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

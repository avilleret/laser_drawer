//~ udp2table.c part of ilda library
//~ receive raw data from udpreceive parse them and send them to tables

#include "m_pd.h"
#define PT_COORD_MAX 32767.5

typedef struct ilda_settings
{
    float scale_x, scale_y, offset_x, offset_y;
    int intensity;
    int mode; //~ 0:analog color, 1:digital color, 2:analog green, 3:digital green
    int invert_x, invert_y, blanking_off, invert_blancking;
    float angle_correction, end_line_correction, scan_freq;
} t_ilda_settings;

typedef struct udp2table
{
  t_object x_ob;
  t_outlet *m_dataout;
  t_garray *xarray, *yarray, *redarray, *greenarray, *bluearray;
  t_ilda_settings settings;
} t_udp2table;

t_class *udp2table_class;

void decode_settings(t_udp2table *x, unsigned int ac, t_atom* av){
    int i = 18;
    x->settings.offset_x = (float) (((char) av[i].a_w.w_float << 8) + (char) av[i+1].a_w.w_float) / PT_COORD_MAX - 1.;
    i+=2;
    x->settings.offset_y = (float) (((char) av[i].a_w.w_float << 8) + (char) av[i+1].a_w.w_float) / PT_COORD_MAX - 1.;
    i+=2;
    x->settings.scale_x =  (float) (((char) av[i].a_w.w_float << 8) + (char) av[i+1].a_w.w_float) / PT_COORD_MAX - 1.;
    i+=2;
    x->settings.scale_y = (float) (((char) av[i].a_w.w_float << 8) + (char) av[i+1].a_w.w_float) / PT_COORD_MAX - 1.;
    i+=2;
    x->settings.invert_x = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.invert_y = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.blanking_off = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.invert_blancking = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.intensity = (char) av[i++].a_w.w_float;
    x->settings.mode = (char) av[i++].a_w.w_float;
    x->settings.angle_correction = (float) (((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float);
    i+=2;
    x->settings.end_line_correction = (float) (((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float);
    i+=2;
    x->settings.scan_freq = (float) (((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float);
    
    t_atom output_settings[13];
    for ( i=0;i<13;i++ ){
        output_settings[i].a_type=A_FLOAT;
    }
    
    i=0;
    output_settings[i++].a_w.w_float = (float) x->settings.offset_x;
    output_settings[i++].a_w.w_float = (float) x->settings.offset_y;
    output_settings[i++].a_w.w_float = (float) x->settings.scale_x;
    output_settings[i++].a_w.w_float = (float) x->settings.scale_y;
    output_settings[i++].a_w.w_float = (float) x->settings.invert_x;
    output_settings[i++].a_w.w_float = (float) x->settings.invert_y;
    output_settings[i++].a_w.w_float = (float) x->settings.blanking_off;
    output_settings[i++].a_w.w_float = (float) x->settings.invert_blancking;
    output_settings[i++].a_w.w_float = (float) x->settings.intensity;
    output_settings[i++].a_w.w_float = (float) x->settings.mode;
    output_settings[i++].a_w.w_float = (float) x->settings.angle_correction;
    output_settings[i++].a_w.w_float = (float) x->settings.end_line_correction;
    output_settings[i++].a_w.w_float = (float) x->settings.scan_freq;
    
    outlet_anything(x->m_dataout, gensym("settings"), 13, output_settings);    
}

void decode_data(t_udp2table *x, unsigned int ac, t_atom* av){
}

void udp2table_list(t_udp2table *x, t_symbol* s, unsigned int ac, t_atom* av){
   
    //~ validity check
    if ( ac < 17 ) {
        error("this is not an ilda frame");
        return; //~ this is not a ilda frame
    }
    
    char tag[13]="StartNewTrame";
    
    int i;
    for ( i = 0 ; i < 13 ; i++){
        if ((int) av[i].a_w.w_float != tag[i]) {
            error("wrong frame header (bad tag)");
            return;
        }
    }
    
    long unsigned int size = ((char) av[13].a_w.w_float << 24) + ((char) av[14].a_w.w_float << 16) + ((char) av[15].a_w.w_float << 8) + (char) av[16].a_w.w_float;
    
    if ( size != (ac-17) ) {
        error("wrong frame header (bad size)");
        return;
    }
    
    if ( (int) av[17].a_w.w_float ){
        decode_data(x, ac, av);
    } else {
        decode_settings(x, ac, av);
    }
}

void *udp2table_new(void)
{
    t_udp2table *x = (t_udp2table *)pd_new(udp2table_class);
    x->m_dataout = outlet_new((struct t_object *)x,0);
    x->xarray=NULL;
    x->yarray=NULL;
    x->redarray=NULL;
    x->greenarray=NULL;
    x->bluearray=NULL;
    post("udp2table_new");
    return (void *)x;
}

    /* this is called once at setup time, when this code is loaded into Pd. */
void udp2table_setup(void)
{
    post("udp2table_setup");
    udp2table_class = class_new(gensym("udp2table"), (t_newmethod)udp2table_new, 0,
    	sizeof(t_udp2table), 0, 0);
    class_addlist(udp2table_class, udp2table_list);
}



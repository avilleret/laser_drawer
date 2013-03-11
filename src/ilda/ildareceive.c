//~ ildareceive.c part of ilda library
//~ receive raw data from udpreceive parse them and send them to tables

#include "m_pd.h"
#include "string.h"
#include "ilda.h"
#ifdef HAVE_OPENCV 
#include "cv.h"
#endif

typedef struct ildareceive
{
  t_object x_ob;
  t_outlet *m_dataout;
  t_ilda_channel channel[5];
  t_ilda_settings settings;
  lo_server_thread OSC_server;
#ifdef HAVE_OPENCV
  CvMat *map_matrix;
#endif
} t_ildareceive;

t_class *ildareceive_class;

void decode_data(t_ildareceive *x, unsigned int ac, t_atom* av){
    if ( !x->channel[0].array || !x->channel[1].array || !x->channel[2].array || !x->channel[3].array || !x->channel[4].array ){
        error("you should specify x, y, r, g and b before");
        return;
    }
    
    int i=ILDA_HEADER_SIZE,line_strip,j; // first number of points
    line_strip=(((char) av[i].a_w.w_float & 0xFF) << 8 ) | ((char) av[i+1].a_w.w_float & 0xFF); //~ number of line strips
    
    int pt_count = 0, tmp;
    i+=5; //~ first line strip
    
    for ( j=0 ; j < line_strip && i < (ac-2) ; j++ ){
        tmp=(int) (((char) av[i].a_w.w_float & 0xFF) << 8 ) | ((char) av[i+1].a_w.w_float & 0xFF);
        //~ printf("strip %d/%d with %d point\n", j, line_strip, tmp);
        pt_count+=tmp;
        i+=tmp*4+5;
    }
    
    int size=pt_count;
    //~ printf("total number of point : %d\n", size);
    
    //~ resize tables
    for ( i=0; i<5; i++ ){
        if (!garray_getfloatwords(x->channel[i].array, &(x->channel[i].vecsize), &(x->channel[i].vec))){
            error("%s: bad template", x->channel[i].arrayname->s_name);
            return;
        } else if ( x->channel[i].vecsize != size ){
            garray_resize_long(x->channel[i].array,size);
            if (!garray_getfloatwords(x->channel[i].array, &(x->channel[i].vecsize), &(x->channel[i].vec))){
                error("%s: can't resize correctly", x->channel[i].arrayname->s_name);
                return;
            } 
        }
    }
    
    i = ILDA_HEADER_SIZE + 2; // first line strip
    float r,g,b;
    j=0;
    int strip;
    for ( strip=0 ; strip < line_strip ; strip++ ){
        //~ printf("line strip %d/%d\n", strip, line_strip);
        if ( x->settings.invert[2] ){
            r=1.-av[i].a_w.w_float / 255;
            g=1.-av[i+1].a_w.w_float / 255;
            b=1.-av[i+2].a_w.w_float / 255;
        } else {
            r=av[i].a_w.w_float / 255;
            g=av[i+1].a_w.w_float / 255;
            b=av[i+2].a_w.w_float / 255;
        }
        i+=3;
        
        pt_count = (int) (((char) av[i].a_w.w_float & 0xFF) << 8 ) | ((char) av[i+1].a_w.w_float & 0xFF);
        //~ printf("strip with %d points\n",pt_count); 
        i+=2;
        
        int k;
        for ( k = 0 ; k < pt_count && j < size && i < av-4 ; k++){  
            //~ i:index in the input frame, j:index in the table, k:index in the point table (subtable of input frame)
            float tmpx = (float) ((((char) av[i].a_w.w_float & 0xFF) << 8 ) | ((char) av[i+1].a_w.w_float & 0xFF))/PT_COORD_MAX - 1.;
            i+=2;
            float tmpy = (float) ((((char) av[i].a_w.w_float & 0xFF) << 8 ) | ((char) av[i+1].a_w.w_float & 0xFF))/PT_COORD_MAX - 1.;
            i+=2;
            
            if ( x->settings.invert[0] ){
                x->channel[ILDA_CH_X].vec[j].w_float= (1-tmpx) * x->settings.scale[1] + x->settings.offset[0];
            } else {
                x->channel[ILDA_CH_X].vec[j].w_float= tmpx * x->settings.scale[1] + x->settings.offset[0];
            }
            
            if ( x->settings.invert[1] ){
                x->channel[ILDA_CH_Y].vec[j].w_float= (1-tmpy) * x->settings.scale[0] + x->settings.offset[1];
            } else {
                x->channel[ILDA_CH_Y].vec[j].w_float= tmpy * x->settings.scale[0] + x->settings.offset[1];
            }
            
            if ( x->settings.blanking_off ) {
                x->channel[ILDA_CH_R].vec[j].w_float=1.;
                x->channel[ILDA_CH_G].vec[j].w_float=1.;
                x->channel[ILDA_CH_B].vec[j].w_float=1.;
            } else {
                x->channel[ILDA_CH_R].vec[j].w_float=r;
                x->channel[ILDA_CH_G].vec[j].w_float=g;
                x->channel[ILDA_CH_B].vec[j].w_float=b;
            }
            
            if ( x->settings.invert[2] ){
                x->channel[ILDA_CH_R].vec[j].w_float=1-x->channel[ILDA_CH_R].vec[j].w_float;
                x->channel[ILDA_CH_G].vec[j].w_float=1-x->channel[ILDA_CH_G].vec[j].w_float;
                x->channel[ILDA_CH_B].vec[j].w_float=1-x->channel[ILDA_CH_B].vec[j].w_float;
            }
            
            //~ printf("%d\t%.5f\t%.5f\t%.5f\n", j, x->channel[ILDA_CH_X].vec[j].w_float, x->channel[ILDA_CH_Y].vec[j].w_float, x->channel[ILDA_CH_R].vec[j].w_float);
            j++;
        }
    }
    for ( i=0;i<5;i++) {
        garray_redraw(x->channel[i].array);
    }

    
    x=NULL;
    ac=0;
    av=NULL;
}

static void ildareceive_error(int num, const char *msg, const char *path)
{
    error("ildareceive error %d in path %s: %s\n", num, path, msg);
}

int ildareceive_offset(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    int i;
    for(i=0; i<argc; i++){
        x->settings.offset[i]=argv[i]->f;
    }
    return 0;
}

int ildareceive_scale(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    int i;
    for(i=0; i<argc; i++){
        x->settings.scale[i]=argv[i]->f;
    }
    return 0;
}

int ildareceive_invert(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    int i;
    for(i=0; i<argc; i++){
        x->settings.invert[i]=argv[i]->f;
    }
    return 0;
}

int ildareceive_intensity(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    int i;
    for(i=0; i<argc; i++){
        x->settings.intensity[i]=argv[i]->f;
    }
    return 0;
}

int ildareceive_blanking_off(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    x->settings.blanking_off=argv[0]->f;
    return 0;
}

int ildareceive_angle_correction(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    x->settings.angle_correction=argv[0]->f;
    return 0;
}

int ildareceive_end_line_correction(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    x->settings.end_line_correction=argv[0]->f;
    return 0;
}

int ildareceive_scan_freq(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    x->settings.scan_freq=argv[0]->f;
    return 0;
}

int ildareceive_array(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    post("array method, path : %s", path);
    return 0;
}

int generic_handler(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    int i;

    printf("path: <%s>\n", path);
    for (i=0; i<argc; i++) {
        if (types[i]=='b'){
            lo_blob b = argv[i];
            printf("c'est un blob de taille : %d\n", lo_blob_datasize(b)/sizeof(t_word));
            int channel=-1;
            if(!strcmp(path,"/array/x")){
                channel=ILDA_CH_X;
            } else if(!strcmp(path,"/array/y")) {
                channel=ILDA_CH_Y;
            }
            if (channel<0) return 1;
            printf("memcpy to array %d\n", channel);
            //~ memcpy(lo_blob_dataptr(b), x->channel[channel].vec, lo_blob_datasize(b));
            printf("done\n");
            garray_redraw(x->channel[channel].array);
            t_word *data = lo_blob_dataptr(b);
            int j;
            for (j=0; j<10; j++) {
                printf("d[%d]=%.2f\n", j, data[j].w_float);
            }
        }
        printf("arg %d '%c', argc : %d", i, types[i], argc);
        lo_arg_pp(types[i], argv[i]);
        printf("\n");
    }
    printf("\n");
    fflush(stdout);

    return 1;
}

static void ildareceive_bind ( t_ildareceive *x, t_float port)
{
    if(x->OSC_server){
        error("receiveilda : already bounded to port %d", lo_server_thread_get_port(x->OSC_server));
        return;
    }
    
    char portstr[30];
    sprintf(portstr, "%d", (int) port);
    x->OSC_server = lo_server_thread_new(portstr, ildareceive_error);
    
    //~ lo_server_thread_add_method(x->OSC_server, "/setting", NULL, ildareceive_parse_ssetting, x);
    
    lo_server_thread_add_method(x->OSC_server, NULL, NULL, generic_handler, x);
    lo_server_thread_add_method(x->OSC_server, "/setting/offset", "fff", ildareceive_offset, x);
    lo_server_thread_add_method(x->OSC_server, "/setting/scale", "fff", ildareceive_scale, x);
    lo_server_thread_add_method(x->OSC_server, "/setting/invert", "fff", ildareceive_invert, x);
    lo_server_thread_add_method(x->OSC_server, "/setting/intensity", "fff", ildareceive_intensity, x);
    lo_server_thread_add_method(x->OSC_server, "/setting/blanking_off", "f", ildareceive_blanking_off, x);
    lo_server_thread_add_method(x->OSC_server, "/setting/angle_correction", "f", ildareceive_angle_correction, x);
    lo_server_thread_add_method(x->OSC_server, "/setting/end_line_correction", "f", ildareceive_end_line_correction, x);
    lo_server_thread_add_method(x->OSC_server, "/setting/scan_freq", "f", ildareceive_scan_freq, x);
    
    lo_server_thread_add_method(x->OSC_server, "/array/x", NULL, ildareceive_array, x);
    
    if ( lo_server_thread_start(x->OSC_server) < 0 ){
        error("ildareceive : can't start server");
        outlet_float(x->m_dataout, 0);
    } else {
        outlet_float(x->m_dataout, 1);
    }
}

static void ildareceive_settab ( t_ildareceive *x, t_symbol* s, unsigned int ac, t_atom* av )
{
    if (ac%2 != 0 ) {
		error("wong arg number, usage : settab <x|y|r|g|b> <table name>");
		return;
	}
    int i=0;
	for(i=0;i<ac;i++){
        if ( av[i].a_type != A_SYMBOL){
            error("wrong arg type, settab args must be symbol");
            return;
        }
	}
    
    for(i=0;i<ac/2;i++){
        t_garray *tmp_array;
        tmp_array = NULL;
        tmp_array = (t_garray *)pd_findbyclass(av[2*i+1].a_w.w_symbol, garray_class);
        if (tmp_array){
            int index=0;
            if ( strcmp( av[2*i].a_w.w_symbol->s_name, "x") == 0){
                index=ILDA_CH_X;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "y") == 0){
                index=ILDA_CH_Y;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "r") == 0){
                index=ILDA_CH_R;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "g") == 0){
                index=ILDA_CH_G;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "b") == 0){
                index=ILDA_CH_B;
            }
            
            x->channel[index].array=tmp_array;
            x->channel[index].arrayname=av[2*i+1].a_w.w_symbol;
        } else {
            error("%s: no such array", av[2*i+1].a_w.w_symbol->s_name);           
        }
    } 
}

void *ildareceive_new(void)
{
    t_ildareceive *x = (t_ildareceive *)pd_new(ildareceive_class);
    x->m_dataout = outlet_new((struct t_object *)x,0);
    
    x->OSC_server = NULL;
    
    x->settings.offset[0]=0.;
    x->settings.offset[1]=0.;
    x->settings.scale[0]=1.;
    x->settings.scale[1]=1.;
    x->settings.invert[0]=0.;
    x->settings.invert[1]=0.;
    x->settings.invert[2]=0.;
    x->settings.blanking_off=0.;
    x->settings.intensity[0]=1.;
    x->settings.intensity[1]=1.;
    x->settings.intensity[2]=1.;
    x->settings.mode=0;
    x->settings.angle_correction=0.;
    x->settings.end_line_correction=0.;
    x->settings.scan_freq=10;
    
    x->channel[ILDA_CH_X].channelname=gensym("x");
    x->channel[ILDA_CH_Y].channelname=gensym("y");
    x->channel[ILDA_CH_R].channelname=gensym("r");
    x->channel[ILDA_CH_G].channelname=gensym("g");
    x->channel[ILDA_CH_B].channelname=gensym("b");
    
    int i;
    for ( i=0;i<5;i++ ){
        x->channel[i].array=NULL;
        x->channel[i].arrayname=NULL;
    }
    
    post("sizeof t_float %d, sizeof t_word %d", sizeof(t_float), sizeof(t_word));
    
    return (void *)x;
}

static void ildareceive_free(t_ildareceive *x)
{
    if (x->OSC_server){
        lo_server_thread_stop(x->OSC_server);
    }
}

    /* this is called once at setup time, when this code is loaded into Pd. */
void ildareceive_setup(void)
{
    ildareceive_class = class_new(gensym("ildareceive"), (t_newmethod)ildareceive_new, (t_newmethod)ildareceive_free,
    	sizeof(t_ildareceive), 0, 0);
    class_addmethod(ildareceive_class, (t_method) ildareceive_settab, gensym("settab"), A_GIMME, 0);
    class_addmethod(ildareceive_class, (t_method) ildareceive_bind, gensym("bind"), A_FLOAT, 0);
}



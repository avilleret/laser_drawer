/* code for "ildasend" pd class.  This takes two messages: floating-point
numbers, and "rats", and just prints something out for each message. */

#include "m_pd.h"
#include "ilda.h"
#ifdef HAVE_OPENCV 
#include "cv.h"
#endif

#define MAXI(a,b) (a>=b?a:b)
#define MINI(a,b) (a<=b?a:b)
#define CLIP(a,b,c) MINI(MAXI(a,b),c)
#define PT_COORD_MIN -320

    /* the data structure for each copy of "ildasend".  In this case we
    only need pd's obligatory header (of type t_object). */
typedef struct ildasend
{
  t_object x_ob;
  t_outlet *m_dataout;
  t_garray *xarray, *yarray, *colorarray;
  char *raw_data;
  short *short_data[3];
  t_ilda_channel channel[5];
  unsigned int buffer_size, list_length;
  t_atom *trame;
  t_ilda_settings settings;
  float perspective_correction;
  lo_address OSC_destination;
  t_float dst_point[8];

} t_ildasend;

    /* this is called back when ildasend gets a "bang" message */
void ildasend_bang(t_ildasend *x)
{
    if( !x->OSC_destination ){
        pd_error(x,"ildasend: not connected");
        return;
    }
    
    int i;
    lo_blob* blob[5];
    
    for (i=0;i<5;i++) {
        blob[i]=NULL;
        
        if ( !x->channel[i].arrayname ){
            pd_error(x,"ildasend: please set 5 tables to receive data (%s is empty)...", x->channel[i].channelname->s_name);
            return;
        }
        
        x->channel[i].array=(t_garray *)pd_findbyclass(x->channel[i].arrayname, garray_class);
        if ( !x->channel[i].array ){
            pd_error(x,"ildasend: can't find array %s",x->channel[i].channelname->s_name);
            return;
        }
        if (!garray_getfloatwords(x->channel[i].array, &x->channel[i].vecsize, &x->channel[i].vec)){
            pd_error(x,"ildasend: error when getting data from array %s...",x->channel[i].arrayname->s_name);
        }
    }
    
    if ( !((x->channel[0].vecsize & x->channel[1].vecsize & x->channel[2].vecsize & x->channel[3].vecsize ) == x->channel[4].vecsize) )
    {
        pd_error(x, "all tables should have the same size");
        return;
    }
    
    for (i=0;i<5;i++){
        if ( sizeof(t_float) == sizeof(t_word) ) {
            blob[i] = lo_blob_new(x->channel[i].vecsize*sizeof(t_word),x->channel[i].vec);            
        } else {
            int j;
            t_float *data = malloc(x->channel[i].vecsize*sizeof(t_float));
            for ( j=0; j<x->channel[i].vecsize; j++ ){
                data[j]=x->channel[i].vec[j].w_float;
            }
            blob[i] = lo_blob_new(x->channel[i].vecsize*sizeof(t_float),data);
            free(data);
        }
    }
    
    lo_send(x->OSC_destination, "/arrays", "bbbbb", blob[0], blob[1], blob[2], blob[3], blob[4]);

    for (i=0;i<5;i++){
        if (blob[i]) {
            lo_blob_free(blob[i]);
            blob[i]=NULL;
        }
    }
}

    /* this is called back when ildasend gets a "float" message (i.e., a
    number.) */

void ildasend_float(t_ildasend *x, t_float a)
{
    t_word *vec[3];
    unsigned int nb_point, tab_size, i;
    int size[3];
    x->list_length=0;
    
	if( !x->xarray || !x->yarray || !x->colorarray ){
		pd_error(x,"ildasend: settab first");
		return;
	}
	
	// copy data from table to vec array
    if (!garray_getfloatwords(x->xarray, &(size[0]), &(vec[0]))) error("error when getting data from array...");
    if (!garray_getfloatwords(x->yarray, &(size[1]), &(vec[1]))) error("error when getting data from array...");
    if (!garray_getfloatwords(x->colorarray, &(size[2]), &(vec[2]))) error("error when getting data from array...");

    tab_size=MINI(MINI(size[0],size[1]),size[2]);
    if (tab_size>x->buffer_size){
		error("buffer overflow, data truncated...");
		tab_size=x->buffer_size;
	}
    a=MAXI(10,a);
	nb_point=MINI(a,tab_size);

	
	t_atom *ap, *bp;
	ap = &x->trame[3]; // position dans la trame

	unsigned int nb_segment=0;

	for ( i=0 ; i<nb_point ; ){
		
		short cur_color = CLIP(x->short_data[2][i],0,255);
		int seg_pt_nb=0; // compteur du nombre de points du segment
		
		ap++->a_w.w_float = (float) cur_color; // red
		ap++->a_w.w_float = (float) cur_color; // green
		ap++->a_w.w_float = (float) cur_color; // blue
		x->list_length+=3;
		cur_color = x->short_data[2][i]; // non-clipped value for comparaison above
		bp=ap+2;
		
		for ( ; ; ){ // infinite loop ?
			bp++->a_w.w_float= (float) ((x->short_data[0][i] & 0xff00) >> 8); // MSB du x du point
			bp++->a_w.w_float= (float) (x->short_data[0][i] & 0xff); // LSB du x du point
			bp++->a_w.w_float= (float) ((x->short_data[1][i] & 0xff00) >> 8); // MSB du y du point
			bp++->a_w.w_float= (float) (x->short_data[1][i] & 0xff); // LSB du y du point
			i++;
			seg_pt_nb++;
			x->list_length+=4;
			
			if ( i>=nb_point ) { /* printf("i>=nb_point (%d >= %d): break !\n", i, nb_point); */ break;}
			if (cur_color!=x->short_data[2][i]) { /* printf("cur_color changed\n"); */ break;} // break the loop !
		}
		ap++->a_w.w_float= (float) ((seg_pt_nb & 0xff00)>>8); // MSB du nb de point du segment
		ap++->a_w.w_float= (float) (seg_pt_nb & 0xff); // LSB du nb de point du segment
		x->list_length+=2;
		ap=bp;
		nb_segment++;
	}
	x->trame[0].a_w.w_float = 1.; // trame status
	x->trame[1].a_w.w_float = (float) ((nb_segment & 0xff00) >> 8); // MSB du nb de segment
	x->trame[2].a_w.w_float = (float) (nb_segment & 0xff); // LSB du nb de segment
	x->list_length+=3;
	
	//~ printf("send %d bytes\n", x->list_length);
	outlet_anything(x->m_dataout, gensym("frame"), x->list_length, x->trame);

	
	ap=NULL;
	
    x=NULL; /* don't warn about unused variables */
}

void ildasend_send_setting(t_ildasend *x)
{
    if ( x->OSC_destination ){
        lo_send(x->OSC_destination, "/setting/offset", "fff", x->settings.offset[0], x->settings.offset[1], x->settings.offset[2] );
        lo_send(x->OSC_destination, "/setting/scale", "fff", x->settings.scale[0], x->settings.scale[1], x->settings.scale[2] );
        lo_send(x->OSC_destination, "/setting/invert", "fff", x->settings.invert[0], x->settings.invert[1], x->settings.invert[2] );
        lo_send(x->OSC_destination, "/setting/intensity", "fff", x->settings.intensity[0], x->settings.intensity[1], x->settings.intensity[2] );
        lo_send(x->OSC_destination, "/setting/blanking_off", "i", x->settings.blanking_off );
        lo_send(x->OSC_destination, "/setting/angle_correction", "f", x->settings.angle_correction );
        lo_send(x->OSC_destination, "/setting/end_line_correction", "f", x->settings.end_line_correction );
        lo_send(x->OSC_destination, "/setting/scan_freq", "f", x->settings.scan_freq );
        lo_send(x->OSC_destination, "/setting/dst_point", "ffffffff", x->dst_point[0], x->dst_point[1], x->dst_point[2], x->dst_point[3], x->dst_point[4], x->dst_point[5], x->dst_point[6], x->dst_point[7] );
        lo_send(x->OSC_destination, "/setting/perspective_correction", "f", x->perspective_correction );
    }
}

    /* this is called when ildasend gets the message, "settab". */
void ildasend_settab ( t_ildasend *x, t_symbol* s, unsigned int ac, t_atom* av )
{
    if (ac%2 != 0 ) {
		pd_error(x,"ildasend: wong arg number, usage : settab <x|y|r|g|b> <table name>");
		return;
	}
    int i=0;
	for(i=0;i<ac;i++){
        if ( av[i].a_type != A_SYMBOL){
            pd_error(x,"ildasend: wrong arg type, settab args must be symbol");
            return;
        }
	}
    
    for(i=0;i<ac/2;i++){
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
        if ( !(t_garray *)pd_findbyclass(av[2*i+1].a_w.w_symbol, garray_class) ){
            pd_error(x,"ildasend: can't find array %s",av[2*i+1].a_w.w_symbol->s_name);
        }
        x->channel[index].arrayname=av[2*i+1].a_w.w_symbol;
    } 
}

void ildasend_connect(t_ildasend *x, t_symbol* hostname, float port){
    if ( x->OSC_destination ) {
        pd_error(x,"ildasend: already connected");
        return;
    }
    
    char portstr[30];
    sprintf(portstr, "%d", (int) port);
    x->OSC_destination = lo_address_new(hostname->s_name,portstr);
    
    if ( x->OSC_destination ){
        outlet_float(x->m_dataout, 1.);
    } else {
        outlet_float(x->m_dataout, 0.);
    }
    return;
}

void ildasend_disconnect(t_ildasend *x){
    lo_address_free(x->OSC_destination);
    x->OSC_destination=NULL;
    
    outlet_float(x->m_dataout, 0.);
}

void ildasend_scale(t_ildasend *x, t_symbol* s, t_float scalex, t_float scaley, t_float scalez)
{
    x->settings.scale[0]=scalex;
    x->settings.scale[1]=scaley;
    x->settings.scale[2]=scalez;
    
    if ( x->OSC_destination ){
        lo_send(x->OSC_destination, "/setting/scale", "fff", x->settings.scale[0], x->settings.scale[1], x->settings.scale[2] );
    }

}

void ildasend_dst_point(t_ildasend *x, t_symbol* s, int ac, t_atom *av)
{  
    int i;
	if (ac != 8) {
		pd_error(x,"ildasend: wrong arg number : dst_point message need 8 float arg (4 x,y pairs)");
		return;
	}
	
	for (i=0;i<8;i++){
		if (av[i].a_type != A_FLOAT) {
			pd_error(x,"ildasend: wrong arg type : dst_point message need 8 float arg (4 x,y pairs)");
			return;
		}
	}
    
    for (i=0;i<8;i++){
        x->dst_point[i]=av[i].a_w.w_float;
    }
        
    if ( x->OSC_destination ){
        lo_send(x->OSC_destination, "/setting/dst_point", "ffffffff", x->dst_point[0], x->dst_point[1], x->dst_point[2], x->dst_point[3], x->dst_point[4], x->dst_point[5], x->dst_point[6], x->dst_point[7] );
    }
}

void ildasend_offset(t_ildasend *x, t_symbol* s, t_float offsetx, t_float offsety, t_float offsetz)
{
    x->settings.offset[0]=offsetx;
    x->settings.offset[1]=offsety;
    x->settings.offset[2]=offsetz;
    
    if ( x->OSC_destination ){
        lo_send(x->OSC_destination, "/setting/offset", "fff", x->settings.offset[0], x->settings.offset[1], x->settings.offset[2] );
    }
}

void ildasend_invert(t_ildasend *x, t_symbol* s, t_float invertx, t_float inverty, t_float invertz)
{
    x->settings.invert[0]=invertx;
    x->settings.invert[1]=inverty;
    x->settings.invert[2]=invertz;
    
    if ( x->OSC_destination ){
        lo_send(x->OSC_destination, "/setting/invert", "fff", x->settings.invert[0], x->settings.invert[1], x->settings.invert[2] );
    }
}

void ildasend_intensity(t_ildasend *x, t_symbol* s, t_float intensityx, t_float intensityy, t_float intensityz)
{
    x->settings.intensity[0]=intensityx;
    x->settings.intensity[1]=intensityy;
    x->settings.intensity[2]=intensityz;
    
    if ( x->OSC_destination ){
        lo_send(x->OSC_destination, "/setting/intensity", "fff", x->settings.intensity[0], x->settings.intensity[1], x->settings.intensity[2] );
    }
}

void ildasend_end_line_correction(t_ildasend *x, t_symbol* s, t_float correction)
{
    x->settings.end_line_correction=correction;
    
    if ( x->OSC_destination ){
        lo_send(x->OSC_destination, "/setting/end_line_correction", "f", x->settings.end_line_correction );
    }
}

void ildasend_scan_freq(t_ildasend *x, t_symbol* s, t_float scan_freq)
{
    x->settings.scan_freq=scan_freq;

    if ( x->OSC_destination ){
        lo_send(x->OSC_destination, "/setting/scan_freq", "f", x->settings.scan_freq );
    }
    return;
}

void ildasend_perspective_correction(t_ildasend *x, t_symbol* s, t_float f)
{
	x->perspective_correction=(f!=0);
    
    if ( x->OSC_destination ){
        lo_send(x->OSC_destination, "/setting/perspective_correction", "f", x->perspective_correction );
    }
    
	return;

}

void *ildasend_free(t_ildasend *x){
	free(x->raw_data);
	free(x->short_data[0]);
	free(x->short_data[1]);
	free(x->short_data[2]);
	free(x->trame);
	x->raw_data=NULL;
	x->short_data[0]=NULL;
	x->short_data[1]=NULL;
	x->short_data[2]=NULL;
	x->trame=NULL;


    return;
}

    /* this is a pointer to the class for "ildasend", which is created in the
    "setup" routine below and used to create new ones in the "new" routine. */
t_class *ildasend_class;


    /* this is called when a new "ildasend" object is created. */
void *ildasend_new()
{
    t_ildasend *x = (t_ildasend *)pd_new(ildasend_class);
    x->m_dataout = outlet_new(x,0);
    x->OSC_destination = NULL;
    
    unsigned int i;
    for ( i=0;i<5;i++ ){
        x->channel[i].array=NULL;
        x->channel[i].arrayname=NULL;
    }
        
    x->settings.offset[0]=0.;
    x->settings.offset[1]=0.;
    x->settings.scale[0]=1.;
    x->settings.scale[1]=1.;
    
    x->channel[ILDA_CH_X].channelname=gensym("x");
    x->channel[ILDA_CH_Y].channelname=gensym("y");
    x->channel[ILDA_CH_R].channelname=gensym("r");
    x->channel[ILDA_CH_G].channelname=gensym("g");
    x->channel[ILDA_CH_B].channelname=gensym("b");
    
    x->dst_point[0]=-1;
	x->dst_point[1]=-1;
	x->dst_point[2]=1;
	x->dst_point[3]=-1;
	x->dst_point[4]=1;
	x->dst_point[5]=1;
	x->dst_point[6]=-1;
	x->dst_point[7]=1;

	x->perspective_correction = 0;

	//~ printf("mapMatrix :\n");
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,0), CV_MAT_ELEM(*(x->map_matrix),float,1,0), CV_MAT_ELEM(*(x->map_matrix),float,2,0));
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,1), CV_MAT_ELEM(*(x->map_matrix),float,1,1), CV_MAT_ELEM(*(x->map_matrix),float,2,1));
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,2), CV_MAT_ELEM(*(x->map_matrix),float,1,2), CV_MAT_ELEM(*(x->map_matrix),float,2,2));
	//~ 
    return (void *)x;
}

    /* this is called once at setup time, when this code is loaded into Pd. */
void ildasend_setup(void)
{
    ildasend_class = class_new(gensym("ildasend"), (t_newmethod)ildasend_new, (t_newmethod)ildasend_free, 
			sizeof(t_ildasend), 0, A_DEFFLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_settab, gensym("settab"), A_GIMME, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_dst_point, gensym("dst_point"), A_GIMME, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_perspective_correction, gensym("perspective_correction"), A_FLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_offset, gensym("offset"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_scale, gensym("scale"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_invert, gensym("invert"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_intensity, gensym("intensity"), A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_end_line_correction, gensym("end_line_correction"), A_FLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_scan_freq, gensym("scan_freq"), A_FLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_connect, gensym("connect"), A_SYMBOL, A_FLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_disconnect, gensym("disconnect"), A_CANT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_send_setting, gensym("sendsettings"), A_CANT, 0);
    class_addbang(ildasend_class, ildasend_bang);
    class_addfloat(ildasend_class, ildasend_float);

#ifdef HAVE_OPENCV
    post("ildasend by Antoine Villeret,\n\tbuild on %s at %s with OpenCV support.",__DATE__, __TIME__);
#else    
	post("ildasend by Antoine Villeret,\n\tbuild on %s at %s.",__DATE__, __TIME__);
#endif
}

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
  float perspective_correction;
#ifdef HAVE_OPENCV
  CvMat *map_matrix;
#endif
} t_ildareceive;

t_class *ildareceive_class;

static void ildareceive_perspective_correction(t_ildareceive *x, lo_arg **argv, int argc)
{
#ifdef HAVE_OPENCV 
	// using OpenCV for perspective correction
	CvMat *src, *dst;
    int i,j;
    lo_blob blob[2];
    blob[0]=argv[0];
    blob[1]=argv[1];
    int size = lo_blob_datasize(blob[0])/sizeof(t_float); //~ assume all blobs have the same size
    
    //~ resize arrays
    for ( i=0;i<2;i++) {
        if ( !x->channel[i].arrayname ){
            pd_error(x,"ildareceive: don't be so impatient, settab first...");
            return 0;
        }
        
        x->channel[i].array=(t_garray *)pd_findbyclass(x->channel[i].arrayname, garray_class);
        if ( !x->channel[i].array){
            pd_error(x,"ildareceive: hoops ! where is array %s ??",x->channel[i].arrayname);
            return 0;
        }
        
        garray_resize_long(x->channel[i].array,size);
        if (!garray_getfloatwords(x->channel[i].array, &(x->channel[i].vecsize), &(x->channel[i].vec))){
            pd_error(x,"ildasend: %s: can't resize correctly", x->channel[i].arrayname->s_name);
            return 0;
        }
    }

	src=cvCreateMat(size, 1, CV_32FC2 ); 
	dst=cvCreateMat(size, 1, CV_32FC2 ); 
	
	float *blobptx, *blobpty, *pt,*pt2;
	pt=src->data.fl;
    blobptx=lo_blob_dataptr(blob[0]);
    blobpty=lo_blob_dataptr(blob[1]);
	 
    for ( i=0 ; i<size ; i++ ) {
		*(pt++)=*(blobptx++); // Replacement for CV_MAT_ELEM broken since 2.3.1
		*(pt++)=*(blobpty++); // Replacement for CV_MAT_ELEM broken since 2.3.1
        //~ printf("%d\t%.3f,%.3f\n", i, x->channel[0].vec[i].w_float, x->channel[1].vec[i].w_float);
	}
    
    dst=cvCreateMat(size, 1, CV_32FC2 );
    cvPerspectiveTransform(src, dst, x->map_matrix);

    //~ printf("id\tsrc\t\tdst\n"); pt=src->data.fl;
	pt2=dst->data.fl;
    for ( i=0;i<size;i++ ){
        x->channel[0].vec[i].w_float=*pt2;
        x->channel[1].vec[i].w_float=*(pt2+1);
        //~ x->channel[1].vec[i].w_float=(i/10)%2;
		//~ printf("%d\t(%.3f,%.3f)\t(%.3f,%.3f)\n",i,*pt,*(pt+1),*pt2,*(pt2+1)); pt+=2;
        pt2+=2;
	}

    //~ garray_redraw(x->channel[0].array);
    //~ garray_redraw(x->channel[1].array);
    //~ Release Matrix data
	if (dst!=src) { 
		cvReleaseMat(&dst);
		dst=NULL;
	}
	if (src) {
		cvReleaseMat(&src);
		src=NULL;
	}
    
#else
    pd_error(x,"nonono ! you should build against OpenCV to use perspective correction !!");
#endif

    return;
}

static void ildareceive_error(int num, const char *msg, const char *path)
{
    error("ildasend: ildareceive error %d in path %s: %s\n", num, path, msg);
}

//~ OSC parsing methods

int ildareceive_offset(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    int i;
    t_atom atom_data[3];
    for(i=0; i<3; i++){
        x->settings.offset[i]=argv[i]->f;
        SETFLOAT(atom_data+i, x->settings.offset[i]);
    }
    
    outlet_anything(x->m_dataout, gensym("offset"), 3, atom_data);
    return 0;
}

int ildareceive_scale(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    int i;
    t_atom atom_data[3];
    for(i=0; i<3; i++){
        x->settings.scale[i]=argv[i]->f;
        SETFLOAT(atom_data+i, x->settings.scale[i]);
    }
    
    outlet_anything(x->m_dataout, gensym("scale"), 3, atom_data);
    return 0;
}

int ildareceive_invert(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    int i;
    t_atom atom_data[3];
    for(i=0; i<3; i++){
        x->settings.invert[i]=argv[i]->f;
        SETFLOAT(atom_data+i, x->settings.scale[i]);        
    }
    
    outlet_anything(x->m_dataout, gensym("invert"), 3, atom_data);
    return 0;
}

int ildareceive_intensity(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    int i;
    t_atom atom_data[3];
    for(i=0; i<3; i++){
        x->settings.intensity[i]=argv[i]->f;
        SETFLOAT(atom_data+i, x->settings.intensity[i]);
    }
    
    outlet_anything(x->m_dataout, gensym("intensity"), 3, atom_data);
    return 0;
}

int ildareceive_blanking_off(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    x->settings.blanking_off=argv[0]->f;
    
    t_atom atom_data;
    SETFLOAT(&atom_data, x->settings.blanking_off);
    outlet_anything(x->m_dataout, gensym("blanking_off"), 1, &atom_data);
    return 0;
}

int ildareceive_angle_correction(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    x->settings.angle_correction=argv[0]->f;
    t_atom atom_data;
    SETFLOAT(&atom_data, x->settings.angle_correction);
    outlet_anything(x->m_dataout, gensym("angle_correction"), 1, &atom_data);
    return 0;
}

int ildareceive_end_line_correction(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    x->settings.end_line_correction=argv[0]->f;
    t_atom atom_data;
    SETFLOAT(&atom_data, x->settings.end_line_correction);
    outlet_anything(x->m_dataout, gensym("end_line_correction"), 0, &atom_data);
    return 0;
}

int ildareceive_scan_freq(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    x->settings.scan_freq=argv[0]->f;
    t_atom atom_data;
    SETFLOAT(&atom_data, x->settings.scan_freq);
    outlet_anything(x->m_dataout, gensym("scan_freq"), 0, &atom_data) ;
    return 0;
}

int ildareceive_enable_perspective_correction(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
#ifdef HAVE_OPENCV
	x->perspective_correction=(argv[0]->f!=0);
    post("perspective correction %1f",x->perspective_correction);
	return 0;
#else
	pd_error(x,"ildasend: you need to compile with OpenCV to use this method (perspective_correction)");
	return 0;
#endif
}

int ildareceive_generic_handler(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
    int i=2, rtn=1;
    
    //~ printf("path: %s\n", path);
    if ( argc < 2 ){
        pd_error(x, "ildareceive: ouch ! not enough blob in OSC message !");
    }
    
    if ( x->perspective_correction ){
        ildareceive_perspective_correction(x, argv, argc);
    } else {
        i=0;
    }
    
    for (; i<argc; i++) {
        if (types[i]=='b'){
            lo_blob b = argv[i];
            size_t size = lo_blob_datasize(b)/sizeof(t_float);
            
            if ( !x->channel[i].arrayname ){
                pd_error(x,"ildareceive: don't be so impatient, settab first...");
                return 0;
            }
            
            x->channel[i].array=(t_garray *)pd_findbyclass(x->channel[i].arrayname, garray_class);
            if ( !x->channel[i].array){
                pd_error(x,"ildareceive: hoops ! where is array %s ??",x->channel[i].arrayname);
                return 0;
            }
            
            garray_resize_long(x->channel[i].array,size);
            if (!garray_getfloatwords(x->channel[i].array, &(x->channel[i].vecsize), &(x->channel[i].vec))){
                pd_error(x,"ildasend: %s: can't resize correctly", x->channel[i].arrayname->s_name);
                return 0;
            } 
            
            if ( sizeof(t_float) == sizeof(t_word) ) {
                t_word *data = lo_blob_dataptr(b);
                memcpy(x->channel[i].vec, data, lo_blob_datasize(b));
            } else {
                int j;
                t_float *data = lo_blob_dataptr(b);
                for ( j=0; j<lo_blob_datasize(b)/sizeof(t_float); j++ ){
                    x->channel[i].vec[j].w_float=data[j];
                }
            }
            //~ garray_redraw(x->channel[i].array);
        }
    }
    
    return 0;
}

void ildareceive_dst_point(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, t_ildareceive *x)
{
#ifdef HAVE_OPENCV
	
	CvPoint2D32f src_point[4], dst_point[4];
	
	src_point[0].x=-1.;
	src_point[0].y=1.;
	src_point[1].x=1.;
	src_point[1].y=1.;
	src_point[2].x=1.;
	src_point[2].y=-1.;
	src_point[3].x=-1.;
	src_point[3].y=-1.;
    
	int i;
    for (i=0;i<4;i++){
        dst_point[i].x=argv[2*i]->f;
        dst_point[i].y=argv[2*i+1]->f;
    }
	
	
	cvGetPerspectiveTransform( src_point, dst_point , x->map_matrix );

	//~ printf("mapMatrix :\n");
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,0), CV_MAT_ELEM(*(x->map_matrix),float,1,0), CV_MAT_ELEM(*(x->map_matrix),float,2,0));
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,1), CV_MAT_ELEM(*(x->map_matrix),float,1,1), CV_MAT_ELEM(*(x->map_matrix),float,2,1));
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,2), CV_MAT_ELEM(*(x->map_matrix),float,1,2), CV_MAT_ELEM(*(x->map_matrix),float,2,2));
	return;
#else 
	pd_error(x,"ildasend: you need to compile with OpenCV to use this method (%s)", path);
	return;
#endif
}

void ildareceive_bind ( t_ildareceive *x, t_float port)
{
    if(x->OSC_server){
        pd_error(x,"ildasend: receiveilda : already bounded to port %d", lo_server_thread_get_port(x->OSC_server));
        return;
    }
    
    char portstr[30];
    sprintf(portstr, "%d", (int) port);
    x->OSC_server = lo_server_thread_new(portstr, ildareceive_error);
    
    if ( x->OSC_server){
        lo_server_thread_add_method(x->OSC_server, "/arrays", NULL, ildareceive_generic_handler, x);
        lo_server_thread_add_method(x->OSC_server, "/setting/offset", "fff", ildareceive_offset, x);
        lo_server_thread_add_method(x->OSC_server, "/setting/scale", "fff", ildareceive_scale, x);
        lo_server_thread_add_method(x->OSC_server, "/setting/invert", "fff", ildareceive_invert, x);
        lo_server_thread_add_method(x->OSC_server, "/setting/intensity", "fff", ildareceive_intensity, x);
        lo_server_thread_add_method(x->OSC_server, "/setting/blanking_off", "f", ildareceive_blanking_off, x);
        lo_server_thread_add_method(x->OSC_server, "/setting/angle_correction", "f", ildareceive_angle_correction, x);
        lo_server_thread_add_method(x->OSC_server, "/setting/end_line_correction", "f", ildareceive_end_line_correction, x);
        lo_server_thread_add_method(x->OSC_server, "/setting/scan_freq", "f", ildareceive_scan_freq, x);
        lo_server_thread_add_method(x->OSC_server, "/setting/perspective_correction", "f", ildareceive_enable_perspective_correction, x);
        lo_server_thread_add_method(x->OSC_server, "/setting/dst_point", "ffffffff", ildareceive_dst_point, x);
        
        if ( lo_server_thread_start(x->OSC_server) < 0 ){
            pd_error(x,"ildasend: ildareceive : can't start server");
            t_atom data;
            SETFLOAT(&data,0);
            outlet_anything(x->m_dataout, gensym("connected"), 1, &data);
        } else {
            t_atom data;
            SETFLOAT(&data,1);
            outlet_anything(x->m_dataout, gensym("connected"), 1, &data);
        }
    }
}

void ildareceive_unbind( t_ildareceive *x ){
    lo_server_thread_free(x->OSC_server);
    x->OSC_server=NULL;
    t_atom data;
    SETFLOAT(&data,0);
    outlet_anything(x->m_dataout, gensym("connected"), 1, &data);
}

void ildareceive_settab ( t_ildareceive *x, t_symbol* s, unsigned int ac, t_atom* av )
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
        int index=-1;
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
        
        if ( index >= 0 ) {
            x->channel[index].arrayname=av[2*i+1].a_w.w_symbol;
        } else {
            pd_error(x, "ildareceive: hey dude ! %s is not a valid chanel name !", av[2*i+1].a_w.w_symbol);
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
    
    x->perspective_correction=0;

#ifdef HAVE_OPENCV
	x->map_matrix=cvCreateMat(3,3,CV_32FC1);
	cvSetIdentity(x->map_matrix,cvRealScalar(1));
    printf("mapMatrix :\n");
	printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,0), CV_MAT_ELEM(*(x->map_matrix),float,1,0), CV_MAT_ELEM(*(x->map_matrix),float,2,0));
	printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,1), CV_MAT_ELEM(*(x->map_matrix),float,1,1), CV_MAT_ELEM(*(x->map_matrix),float,2,1));
	printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,2), CV_MAT_ELEM(*(x->map_matrix),float,1,2), CV_MAT_ELEM(*(x->map_matrix),float,2,2));
#endif
    
    return (void *)x;
}

void ildareceive_free(t_ildareceive *x)
{
    if (x->OSC_server) lo_server_thread_stop(x->OSC_server);
#ifdef HAVE_OPENCV
	if (x->map_matrix) cvReleaseMat(&x->map_matrix);
#endif
}

    /* this is called once at setup time, when this code is loaded into Pd. */
void ildareceive_setup(void)
{
    ildareceive_class = class_new(gensym("ildareceive"), (t_newmethod)ildareceive_new, (t_newmethod)ildareceive_free,
    	sizeof(t_ildareceive), 0, 0);
    class_addmethod(ildareceive_class, (t_method) ildareceive_settab, gensym("settab"), A_GIMME, 0);
    class_addmethod(ildareceive_class, (t_method) ildareceive_bind, gensym("bind"), A_FLOAT, 0);
    class_addmethod(ildareceive_class, (t_method) ildareceive_unbind, gensym("unbind"), 0);
}



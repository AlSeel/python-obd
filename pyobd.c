#include <Python.h>

#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define BUFSIZE  8000

static PyObject *SpamError;
static unsigned char msg[BUFSIZE];
static int s;
static int msg_end_pointer = 0;
static int msg_decriptors [8][3];

static void parse_can_msg(struct can_frame frame){
   int i,ff_dl,len;
   struct can_frame FC_frame;

   for(i=0;i<8;i++){
     if(msg_decriptors[i][0] == frame.can_id){
        //cf
        len = msg_decriptors[i][2] - msg_decriptors[i][1];
        len = (len < 7)? len : 7;

        memcpy(&msg[ msg_decriptors[i][1]],&frame.data[1],len);

        //move msg index
        msg_decriptors[i][1] += len;

        break;
     }
	
     // make sure only free slot is used	   
     if(msg_decriptors[i][0] != 0x0){
	continue;
     }
	     
     //send FC
     if ( frame.can_id & CAN_EFF_FLAG ){
	FC_frame.can_id = 0x18DA00F1 | ((0x000000FF &  frame.can_id) << 8);
     }else{
	FC_frame.can_id =  frame.can_id - 8;
     }
     memset(FC_frame.data,0, CAN_MAX_DLEN);
     FC_frame.data[0] = 0x30;
     FC_frame.can_dlc = 3;

     if(frame.data[0] & 0xF0){
        if( write(s, &FC_frame, sizeof(struct can_frame)) < 0 ){
          ;
        }
     }

     //ff
     msg_decriptors[i][0] = frame.can_id;

     //calc LEN
     ff_dl = 0;

     if( frame.data[0] & 0xF0 ){
       ff_dl = (frame.data[0] & 0x0F)<<8;
       ff_dl |= frame.data[1];
     }
     else{
       ff_dl = (frame.data[0] & 0x0F);
     }

     // store can id + length
#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
     msg[msg_end_pointer] =  frame.can_id & 0x1FFFFFFF;
     msg[msg_end_pointer + 4] = *(((unsigned char *) &ff_dl)+1);
     msg[msg_end_pointer + 5] = *(((unsigned char *) &ff_dl)+0);

#else
     frame.can_id &= 0x1FFFFFFF;
     msg[msg_end_pointer] = *(((unsigned char *) &frame.can_id)+ 3);
     msg[msg_end_pointer + 1] =  *(((unsigned char *) &frame.can_id)+ 2);
     msg[msg_end_pointer + 2] =  *(((unsigned char *) &frame.can_id)+ 1);
     msg[msg_end_pointer + 3] =  *(((unsigned char *) &frame.can_id));

     msg[msg_end_pointer + 4] = *(((unsigned char *) &ff_dl)+1);
     msg[msg_end_pointer + 5] = *(((unsigned char *) &ff_dl)+0);
#endif

     if( frame.data[0] & 0xF0 ){
       memcpy(&msg[msg_end_pointer + 6],&frame.data[2],6);
       //current msg index
       msg_decriptors[i][1] =  msg_end_pointer  + 12;
     }
     else{
       memcpy(&msg[msg_end_pointer + 6],&frame.data[1],ff_dl);
       //current msg index
       msg_decriptors[i][1] = msg_end_pointer + 6 + ff_dl;
     }

      // store new end pointer
      msg_end_pointer  += (ff_dl + 6);

      //store end of current msg
      msg_decriptors[i][2] = msg_end_pointer;

      break;
   }

   return;
}

static PyObject * pyobd_send(PyObject *self, PyObject *args) {
    PyObject *bufobj;
    Py_buffer view;
    int tx_id;
    int length;
    int result = 0;
    struct can_frame frame;
    fd_set readSet;
    struct timeval timeout;

    msg_end_pointer  = 0;
    memset(msg_decriptors, 0, sizeof(msg_decriptors));

    if (!PyArg_ParseTuple(args, "iO",&tx_id, &bufobj)) {
      return NULL;
    }

    if (PyObject_GetBuffer(bufobj, &view,PyBUF_SIMPLE) == -1) {
      return NULL;
    }

    length = (view.len <= 6)? view.len : 6;
    memcpy(&frame.data[1], (unsigned char *)view.buf, length);
    frame.can_dlc = length + 1;
    frame.data[0] = length;   //endianness ?
    frame.can_id = tx_id;

    //send  obd request
    if (write(s, &frame, sizeof(struct can_frame)) < 0 ){
       return Py_BuildValue("i", -1);
    }


   //receive response
    while(1){
	FD_ZERO(&readSet);
	FD_SET(s,&readSet);
	//timeout.tv_sec = 10; //for test
	timeout.tv_sec = 0;
	timeout.tv_usec = 50000; //P2CAN


	if(select((s+1),&readSet,NULL,NULL,&timeout) < 0){
	   result = -1;
           break;
	}

	if(!FD_ISSET(s,&readSet)){
	  //timeout
	  break;
	}

	if(read(s,&frame,sizeof(struct can_frame)) < 0){
	   result = -1;
           break;
	}


        if(frame.can_id & CAN_ERR_FLAG){
	   result = -1;
           break;
        }


	if(frame.data[0]==0x03 && frame.data[1]==0x7F && frame.data[3] == 0x78){
	   //neg. response
           timeout.tv_sec = 5; //P2CANEXT
           timeout.tv_usec = 0; //P2CAN
	   continue;
	}

        parse_can_msg(frame);

    }

    PyBuffer_Release(&view);

    return Py_BuildValue("i", result);
}

static PyObject * pyobd_init(PyObject *self, PyObject *args)
{
    char* interface;
    struct sockaddr_can addr;
    can_err_mask_t err_mask = CAN_ERR_MASK;
    struct can_filter rfilter[2];


    if (!PyArg_ParseTuple(args, "s",&interface)) {
      return Py_BuildValue("i", -1);
    }

    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);

    if(s < 0){
      return Py_BuildValue("i", -1);
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = if_nametoindex(interface);
    addr.can_addr.tp.tx_id = 0x7DF;


    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
       close(s);
       return Py_BuildValue("i", -1);
    }

    //set receive filter
    rfilter[0].can_id = 0x7E8;
    rfilter[0].can_mask = 0x7E8;
    rfilter[1].can_id = 0x18DAF100 | CAN_EFF_FLAG;
    rfilter[1].can_mask = (CAN_EFF_FLAG | 0x18DAF100 );

    if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter)) < 0) {
       close(s);
       return Py_BuildValue("i", -1);
    }


    if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,&err_mask, sizeof(err_mask))< 0){
       close(s);
       return Py_BuildValue("i", -1);
    }

    return Py_BuildValue("i", 0);
}


static PyObject * pyobd_close(PyObject *self, PyObject *args)
{
    int result = -1;

    result = close(s);

    return Py_BuildValue("i", result);
}

static PyObject * pyobd_receive(PyObject *self, PyObject *args)
{
    int i;
    PyObject *pylist;// *item;

    pylist = PyList_New(msg_end_pointer);

    if (pylist != NULL) {
      for (i=0; i<msg_end_pointer; i++) {
       // item = PyInt_FromLong((long)buf[i]);
        PyList_SET_ITEM(pylist, i,Py_BuildValue("i",(int)msg[i] ));
      }
    }

    return pylist;
}

static PyMethodDef PyobdMethods[] = {
    {"send",  pyobd_send,METH_VARARGS,"Send obd request."},
    {"receive",  pyobd_receive,METH_VARARGS,"Receive obd request."},
    {"close",  pyobd_close,METH_VARARGS,"Close obd connection."},
    {"init",  pyobd_init,METH_VARARGS,"Open obd connection."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef pyobdmodule = {
   PyModuleDef_HEAD_INIT,
   "pyobd",   /* name of module */
   NULL, /* module documentation, may be NULL */
   -1,       /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
   PyobdMethods
};

PyMODINIT_FUNC PyInit_pyobd(void)
{
    PyObject *m;

    m = PyModule_Create(&pyobdmodule);
    if (m == NULL)
        return NULL;

    SpamError = PyErr_NewException("spam.error", NULL, NULL);
    Py_INCREF(SpamError);
    PyModule_AddObject(m, "error", SpamError);
    return m;
}

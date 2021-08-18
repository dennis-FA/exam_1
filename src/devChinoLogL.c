/* devChinoLogL.c */
/****************************************************************************
 *                         COPYRIGHT NOTIFICATION
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 ****************************************************************************/
/* Author : Makoto Tobiyama 19/Jul/2005 */
/* Modification Log:
 * -----------------
 */
#include        <dbFldTypes.h>

#include        "drvNetMpf.h"
#include        "devNetDev.h"

#ifndef EPICS_REVISION
#include <epicsVersion.h>
#endif
#if EPICS_REVISION == 14 && EPICS_MODIFICATION >= 2
#include <epicsExport.h>
#endif

#define CHINOL_CMND_LENGTH 8  /* not correct */
#define CHINOL_UDP_PORT    11111
#define CHINOL_GET_PROTO chino_get_protocol()
#define CHINOL_MAX_NDATA 5120
#define CHINOL_DATA_OFFSET 3

//LOCAL int finsUseSamePortNumber = MPF_SAMEPORT;
LOCAL unsigned short calCRC(unsigned char cd[], int nmax);
LOCAL int TwoRawToVal(unsigned char bu[],int offset, float *res );
LOCAL long chino_parse_link(
			  struct link *,
			  struct sockaddr_in *,
			  int *,
			  void *
			  );
LOCAL int chino_get_protocol(void);
LOCAL void *chino_calloc(int, int, int, int, int);


typedef struct
{
  int      unit;
  int      type;
  int      chan;
  int      bit;
  int      width;
  int      slvA;
  int      cmd;
  int      start;
  int      ndata;
  int      data_trans;
  char    *lopt;
} CHINOL_LOG;

LOCAL long chino_config_command(uint8_t *, int *, void *, int, int, int *, CHINOL_LOG *, int);
LOCAL long chino_parse_response(uint8_t *, int *, void *, int, int, int *, CHINOL_LOG *, int);

int chinoLogLUseTcp = 1;


LOCAL int chino_get_protocol(void)
{
  if (chinoLogLUseTcp)
    {
      return MPF_TCP;
    }

  return MPF_UDP;
}


LOCAL void *chino_calloc(
		       int unit,
		       int type,
		       int chan,
		       int bit,
		       int width
		       )
{
  CHINOL_LOG *d;

  d = (CHINOL_LOG *) calloc(1, sizeof(CHINOL_LOG));
  if (!d)
    {
      errlogPrintf("devChinoLogL: calloc failed\n");
      return NULL;
    }
      
  d->unit   = unit;
  d->type   = type;
  d->chan   = chan;
  d->bit    = bit;
  d->width  = width;

  return d;
}



#include        "devWaveformChinoLogL.c"





/*********************************************************************************
 * Link field parser for command/response I/O
 *********************************************************************************/
LOCAL long chino_parse_link(
			  struct link *plink,
			  struct sockaddr_in *peer_addr,
			  int *option,
			  void *device
			  )
{
  char *protocol = NULL;
  char *unit  = NULL;
  char *type  = NULL;
  char *addr  = NULL;
  char *route = NULL;
  char *lopt  = NULL;
  CHINOL_LOG *d = (CHINOL_LOG *) device;

  if (parseLinkPlcCommon(
			 plink,
			 peer_addr,
			 &protocol,
			 &route, /* dummy */
			 &unit,
			 &type,
			 &addr,
			 &lopt
			 ))
    {
      errlogPrintf("devChinoLogL: illeagal input specification\n");
      return ERROR;
    }

  errlogPrintf("chino parse link\n");
  if (sscanf(unit,"%x",&d->slvA) !=1)
    {
      errlogPrintf("devChinoLogL :cannot get slave address\n");
    }
  if (sscanf(type,"%x",&d->cmd) !=1)
    {
      errlogPrintf("devChinoLogL :cannot get command\n");
    }


   if (sscanf(addr,"%d",&d->start) != 1)
    {
      errlogPrintf("devChinoLogL : cannot get start address\n");
    }

   if (sscanf(lopt,"%d",&d->data_trans) !=1)
    {
      errlogPrintf("devChinoLogL : cannot get transfer length\n");
    }
   /* errlogPrintf("parse start %d trans %d\n",d->start, d->data_trans); */
  return OK;
}





/******************************************************************************
 * Command constructor for command/response I/O
 ******************************************************************************/
LOCAL unsigned short calCRC(unsigned char cd[], int nmax)
{
  int i,j;
  unsigned short iCRC;
  unsigned short iCY,iP;
  unsigned char iC1,iC2;
                                                                                
  iCRC = 0xffff;
                                                                                
  for (i=0;i<nmax;i++)
    {
      iCRC = iCRC ^ cd[i];
      for (j=1; j<=8; j++)
        {
          iCY = iCRC & 0x1;
          if ((iCRC & 0x8000) == 0x8000)
            {
              iP = 0x4000;
              iCRC = iCRC & 0x7fff;
            }
          else
            {
              iP = 0x0;
            }
          iCRC = iCRC>>1;
          iCRC = iCRC | iP;
          if (iCY == 1){
            iCRC = iCRC ^ 0xa001;
          }
        }
    }
  if ((iCRC & 0x8000) == 0x8000)
    {
      iP = 0x80;
      iCRC = iCRC & 0x7fff;
    }
  else
    {
      iP = 0;
    }
  iC1 = iCRC & 0xff;
  iC2 = ((iCRC & 0x7f00) >>8) | iP;
                                                                                
  return iC1*256 + iC2;
                                                                                
}

LOCAL long chino_config_command(
			      uint8_t *buf,    /* driver buf addr     */
			      int     *len,    /* driver buf size     */
			      void    *bptr,   /* record buf addr     */
			      int      ftvl,   /* record field type   */
			      int      ndata,  /* n to be transferred */
			      int     *option, /* direction etc.      */
			      CHINOL_LOG *d,
			      int      sid
			      )
{
  int nwrite;
  int n;

  int resCRC;

  LOGMSG("devChinoLogL: chino_config_command(0x%08x,%d,0x%08x,%d,%d,%d,0x%08x)\n",
	 buf,*len,bptr,ftvl,ndata,*option,d,0,0);

  n = ndata;

  nwrite = isWrite(*option) ? (d->width)*n : 0;

  if (*len < CHINOL_CMND_LENGTH + nwrite)
    {
      errlogPrintf("devChinoLogL: buffer is running short\n");
      return ERROR;
    }

  buf[ 0] = d->slvA;                    /* slave address */
  buf[ 1] = d->cmd;                       /* command     */
  buf[ 2] = d->start>>8;                /* start address (H)*/
  buf[ 3] = d->start;                   /* start address (L)*/
  buf[ 4] = 0x00;                      /* number (H) */
  buf[ 5] = (d->data_trans)*2;              /* number (L) 0x0c */
 
  resCRC = calCRC(buf,6);

  buf[ 6] = resCRC>>8;                     /* crc (H) */
  buf[ 7] = resCRC     ;                  /* crc (L) */

  /*  errlogPrintf("devChinoLogL: command = %x %x %x %x %x %x %x %x\n",buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]); */
  *len = CHINOL_CMND_LENGTH;

  /*  if (isWrite(*option))
    {
      if (fromRecordVal(
			&buf[CHINOL_CMND_LENGTH],
			d->width,
			bptr,
			d->noff,
			ftvl,
			n,
			CHINOL_NEEDS_SWAP
			))
	{
	  errlogPrintf("devChinoLogL: illeagal command\n");
	  return ERROR;
	}
    }
  */
  /*
  nread  = isRead(*option)? (d->width)*n:0;
  return (CHINOL_DATA_OFFSET + nread);
  */
  return 0;
}




/*******************************************************************************
 * Response parser for command/response I/O
 *******************************************************************************/
LOCAL int TwoRawToVal(unsigned char bu[],int offset, float *res )
{

  float f1;
  
  if ((bu[offset+2] & 0x20) == 0x20){
    f1 = bu[offset]*256 + bu[offset+1];
      }
  else if ((bu[offset] & 0x80) == 0x80){
    f1 = (bu[offset]-255)*256 +(bu[offset+1]-255);
  }
  else{
    f1 = bu[offset]*256+bu[offset+1];
  }

  switch (bu[offset+3] & 0x0f)
    {
    case 0:
      *res = f1;
      break;
    case 1:
      *res = f1*0.1;
      break;
    case 2:
      *res = f1*0.01;
      break;
    case 3:
      *res = f1*0.001;
      break;
    case 4:
      *res = f1*0.0001;
      break;
    default:
      return -1;
    }

  return (bu[offset+3] & 0xf0);
} 


LOCAL long chino_parse_response(
			      uint8_t *buf,    /* driver buf addr     */
			      int     *len,    /* driver buf size     */
			      void    *bptr,   /* record buf addr     */
			      int      ftvl,   /* record field type   */
			      int      ndata,  /* n to be transferred */
			      int     *option, /* direction etc.      */
			      CHINOL_LOG *d,
			      int      sid
			      )
{
  int i;
  int ret;
  float temp[1000],*rawVal,*ptemp;

  rawVal = bptr;

  LOGMSG("devChinoLogL: chino_parse_response(0x%08x,%d,0x%08x,%d,%d,%d,0x%08x)\n",
	 buf,len,bptr,ftvl,ndata,*option,d,0,0);
 
  /*  for (i=0;i<12;i++)
    {
       errlogPrintf("devChinoLogL buffer %d = %x %x %x %x\n",i*4,buf[CHINOL_DATA_OFFSET+i*4],buf[CHINOL_DATA_OFFSET+i*4+1],buf[CHINOL_DATA_OFFSET+i*4+2],buf[CHINOL_DATA_OFFSET+i*4+3]);
       } */
  if (isRead(*option))
    {
      for (i=0;i<(d->data_trans);i++)
	{
	  ret = TwoRawToVal(buf,CHINOL_DATA_OFFSET+i*4,&temp[i]);
	  if (ret != 0){
	    switch (ret & 0xf0){
	    case 0x10:
	      temp[i]=-10.0;
	      break;
	    case 0x20:
	      temp[i]=10.0;
	      break;
	    case 0x30:
	      temp[i]= -1000.0;
	      break;
	    default:
	      temp[i] = -1000.0;
	      /* errlogPrintf("devGhinoLog Emergency code for %d =%x\n",i,ret); */
	    }
	  }
	}
    }
  
  ndata = d->data_trans;
  ptemp = temp;
  i= ndata;
  while (i--)
    {
      *rawVal++ = (float) *ptemp ++;
    }
  ret = 0;

  return ret;
}



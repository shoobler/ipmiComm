#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include <math.h> /* can be removed with ipmiSensorConversion */

#include <registry.h>
#include <alarm.h>
#include <dbAccess.h>
#include <errlog.h>
#include <alarm.h>
#include <dbDefs.h>
#include <dbAccess.h>
#include <recGbl.h>
#include <recSup.h>
#include <devSup.h>
#include <aiRecord.h>
#include <stringinRecord.h>
#include <mbboRecord.h>
#include <mbbiRecord.h>
#include <boRecord.h>
#include <epicsExport.h>
#include <asynDriver.h>
#include <dbScan.h>

#include <ipmiDef.h>
#include <ipmiMsg.h>
#include <drvMch.h>
#include <devMch.h>

#undef DEBUG

/* Device support prototypes */
static long init_ai_record(struct aiRecord *pai);
static long read_ai(struct aiRecord *pai);
/*static long ai_fru_ioint_info(int cmd, struct aiRecord *pai, IOSCANPVT *iopvt);*/

static long init_bo_record(struct  boRecord *pbo);
static long write_bo(struct boRecord *pbo);

static long init_mbbi(struct mbbiRecord *pmbbi);
static long init_mbbi_record(struct mbbiRecord *pmbbi);
static long read_mbbi(struct mbbiRecord *pmbbi);
static long mbbi_ioint_info(int cmd, struct mbbiRecord *pmbbi, IOSCANPVT *iopvt);

static long init_mbbo_record(struct mbboRecord *pmbbo);
static long write_mbbo(struct mbboRecord *pmbbo);

static long init_fru_ai_record(struct aiRecord *pai);
static long read_fru_ai(struct aiRecord *pai);

static long init_fru_stringin_record(struct stringinRecord *pstringin);
static long read_fru_stringin(struct stringinRecord *pstringin);
/*static long stringin_fru_ioint_info(int cmd, struct stringinRecord *pstringin, IOSCANPVT *iopvt);*/

/* global struct for devSup */
typedef struct {
        long            number;
        DEVSUPFUN       report;
        DEVSUPFUN       init;
        DEVSUPFUN       init_record;
        DEVSUPFUN       get_ioint_info;
        DEVSUPFUN       read_write;
        DEVSUPFUN       special_linconv;
} MCH_DEV_SUP_SET;

/* Add reporting */
MCH_DEV_SUP_SET devAiMch         = {6, NULL, NULL, init_ai_record,           NULL,                    read_ai,           NULL};
MCH_DEV_SUP_SET devBoMch         = {6, NULL, NULL, init_bo_record,           NULL,                    write_bo,          NULL};
MCH_DEV_SUP_SET devMbboMch       = {6, NULL, NULL, init_mbbo_record,         NULL,                    write_mbbo,        NULL};
MCH_DEV_SUP_SET devMbbiMch       = {6, NULL, init_mbbi, init_mbbi_record,    mbbi_ioint_info,         read_mbbi,         NULL};
MCH_DEV_SUP_SET devAiFru         = {6, NULL, NULL, init_fru_ai_record,       NULL,                    read_fru_ai,       NULL};
MCH_DEV_SUP_SET devStringinFru   = {6, NULL, NULL, init_fru_stringin_record, NULL,                    read_fru_stringin, NULL};

epicsExportAddress(dset, devAiMch);
epicsExportAddress(dset, devBoMch);
epicsExportAddress(dset, devMbbiMch);
epicsExportAddress(dset, devMbboMch);
epicsExportAddress(dset, devAiFru);
epicsExportAddress(dset, devStringinFru);


/*--- Register functions stolen from devBusMapped ---*/

/* just any unique address */
static void     *registryId = (void*)&registryId;
static void     *ioRegistryId = (void*)&ioRegistryId;
static void     *ioscanRegistryId = (void*)&ioscanRegistryId;

/* Register a device's base address and return a pointer to a
 * freshly allocated 'MchDev' struct or NULL on failure.
 */
MchDev
devMchRegister(const char *name)
{
MchDev rval = 0, d;

        if ( (d = malloc(sizeof(*rval) + strlen(name))) ) {
                /* pre-load the allocated structure -  'registryAdd()'
                 * is atomical...
                 */
                /*d->baseAddr = baseAddress;*/
                strcpy((char*)d->name, name);
                if ( (d->mutex = epicsMutexCreate()) ) {
                        /* NOTE: the registry keeps a pointer to the name and
                         *       does not copy the string, therefore we keep one.
                         *       (_must_ pass d->name, not 'name'!!)
                         */
                        if ( registryAdd( registryId, d->name, d ) ) {
                                rval = d; d = 0;
                        }
                }
        }

        if (d) {
                if (d->mutex)
                        epicsMutexDestroy(d->mutex);
                free(d);
        }
        return rval;
}

/* Find the 'MchDev' of a registered device by name */
MchDev
devMchFind(const char *name)
{
#ifdef DEBUG
printf("devMchFind: name is %s\n",name);
#endif
	return (MchDev)registryFind(registryId, name);
}


/*--- end stolen ---*/

float 
sensorConversion(SdrFull sdr, uint8_t raw)
{
int l, units, m, b, rexp, bexp;
float value;

	l = SENSOR_LINEAR( sdr->linear );

	m     = TWOS_COMP_SIGNED_NBIT(SENSOR_CONV_M_B( sdr->M, sdr->MTol ), 10 );
	b     = TWOS_COMP_SIGNED_NBIT(SENSOR_CONV_M_B( sdr->B, sdr->BAcc ), 10 );
	rexp  = TWOS_COMP_SIGNED_NBIT(SENSOR_CONV_REXP( sdr->RexpBexp ), 4 );
	bexp  = TWOS_COMP_SIGNED_NBIT(SENSOR_CONV_BEXP( sdr->RexpBexp ), 4 );
	units = sdr->units2;    

       	value = ((m*raw) + (b*pow(10,bexp)))*pow(10,rexp);

	if ( l == SENSOR_CONV_LINEAR )
		value = value;
	if ( l == SENSOR_CONV_LN )
		value = log( value );
	if ( l == SENSOR_CONV_LOG10 )
		value = log10( value );
	if ( l == SENSOR_CONV_LOG2 )
		value = log( value )/log( 2 );
	if ( l == SENSOR_CONV_E )
		value = exp( value );
	if ( l == SENSOR_CONV_EXP10 )
		value = pow( value, 10 );
	if ( l == SENSOR_CONV_EXP2 )
		value = pow( value, 2 );
	if ( l == SENSOR_CONV_1_X )
		value = 1/value;
	if ( l == SENSOR_CONV_SQR )
		value = pow( value, 2);
	if ( l == SENSOR_CONV_CUBE )
		value = pow( value, 1/3 );
	if ( l == SENSOR_CONV_SQRT )
		value = sqrt( value );
	if ( l == SENSOR_CONV_CUBE_NEG1 )
		value = pow( value, -1/3 );

	return value;
}

static long 
init_ai_record(struct aiRecord *pai)
{
MchRec   recPvt  = 0; /* Info stored with record */
MchDev   mch     = 0; /* MCH device data structures */
char    *node;        /* Network node name, stored in parm */
char    *task    = 0; /* Optional additional parameter appended to parm */
char    *p;
short    c, s;
long     status  = 0;
char     str[40];

        if ( VME_IO != pai->inp.type ) {
		status = S_dev_badBus;
		goto bail;
	}

        c = pai->inp.value.vmeio.card;
        s = pai->inp.value.vmeio.signal;

        if ( c < 0 ) {
		status = S_dev_badCard;
		goto bail;
	}

        if ( s < 0 ) {
		status = S_dev_badSignal;
		goto bail;
	}

	if ( ! (recPvt = calloc( 1, sizeof( *recPvt ) )) ) {
		sprintf( str, "No memory for recPvt structure");
		status = S_rec_outMem;
		goto bail;
	}

	/* Break parm into node name and optional parameter */
	node = strtok( pai->inp.value.vmeio.parm, "+" );
	if ( (p = strtok( NULL, "+" )) ) {
		task = p;
		if ( strcmp( task, "TEMP" ) && strcmp( task, "FAN" ) && strcmp( task, "V") && strcmp( task, "I") ) { 
			sprintf( str, "Unknown task parameter %s", task);
			status = S_dev_badSignal;
		}
	}

	/* Find data structure in registry by MCH name */
	if ( (mch = devMchFind( node )) ) {
		recPvt->mch  = mch;
                recPvt->task = task;
		pai->dpvt    = recPvt;
	}
	else {
		sprintf( str, "Failed to locate data structure" );
		status = S_dev_noDeviceFound;
		goto bail;
	}

	/* To do: implement alarms 
	mchData = mch->udata;
	sdr     = &mchData->sens[s].sdr;

	if ( sdr->recType == SDR_TYPE_FULL_SENSOR ) {

		if ( SENSOR_NOMINAL_GIVEN( sdr->anlgChar ) || (sdr->nominal) ) {

			switch ( SENSOR_NOMINAL_FORMAT( sdr->units1 ) ) {

				default:
					goto bail;

				case SENSOR_NOMINAL_UNSIGNED:

					raw = sdr->nominal;
					break;

				case SENSOR_NOMINAL_ONES_COMP:

					raw = ONES_COMP_SIGNED_NBIT( sdr->nominal, 8 );
					break;

				case SENSOR_NOMINAL_TWOS_COMP:

					raw = TWOS_COMP_SIGNED_NBIT( sdr->nominal, 8 );
					break;

				case SENSOR_NOMINAL_NONNUMERIC:
					goto bail;

			}

			nominal = sensorConversion( sdr, raw );
			if ( sdr->units2 == SENSOR_UNITS_DEGC)
				nominal = nominal*9/5 + 32;
		}

	} */

bail:
	if ( status ) {
	       recGblRecordError( status, (void *)pai , (const char *)str );
	       pai->pact=TRUE;
	}

        return status;
}

static long 
read_ai(struct aiRecord *pai)
{
MchRec   recPvt  = pai->dpvt;
MchDev   mch;
MchData  mchData;
char    *task;
uint8_t *data    = malloc(MSG_MAX_LENGTH);
uint8_t  raw, sensor;
uint16_t addr    = 0; /* get addr */
short    index   = pai->inp.value.vmeio.signal; /* Sensor index */
long     status  = NO_CONVERT;
SdrFull  sdr;
int      units;
float    value = 0;

	if ( !recPvt )
		return status;

	mch     = recPvt->mch;
	mchData = mch->udata;
	task    = recPvt->task;

	if ( mchIsAlive[mchData->instance] ) {

		sensor = mchData->sens[index].sdr.number;

		epicsMutexLock( mch->mutex );

		ipmiMsgReadSensor( mchData, data, sensor, addr );
		mchData->sens[index].val = value;
	
		epicsMutexUnlock( mch->mutex );

       		raw = data[IPMI_RPLY_SENSOR_READING_OFFSET];

		sdr = &mchData->sens[index].sdr;

		/* All of our conversions are for Full Sensor SDRs */
		if ( sdr->recType != SDR_TYPE_FULL_SENSOR )
			return status;

		value = sensorConversion( sdr, raw );

		units = sdr->units2;
		pai->rval = value;

		/* Perform appropriate conversion - check units */
		if ( !strcmp( task, "TEMP") ) {
			if ( units == SENSOR_UNITS_DEGC)
				pai->val = value*9/5 + 32;
			else if ( units == SENSOR_UNITS_DEGF)
				pai->val = value;
		}
		else if ( !strcmp( task, "FAN") ) {
			if ( units == SENSOR_UNITS_RPM )
				pai->val  = value;
		}
		else if ( !strcmp( task, "V") ) {
			if ( units == SENSOR_UNITS_VOLTS )
				pai->val  = value;
		}
		else if ( !strcmp( task, "I") ) {
			if ( units == SENSOR_UNITS_AMPS )
				pai->val  = value;
		}

#ifdef DEBUG
printf("read_ai: %s sensor index is %i, task is %s, sensor number is %i, value is %.0f, rval is %i\n",
    pai->name, index, task, mchData->sens[index].sdr.number, pai->val, pai->rval);
#endif
		pai->udf = FALSE;
		return status;
	}
	else {
		recGblSetSevr( pai, READ_ALARM, INVALID_ALARM );
		return ERROR;
	}
}

static long 
init_bo_record(struct boRecord *pbo)
{
MchRec  recPvt  = 0; /* Info stored with record */
MchDev  mch     = 0; /* MCH device data structures */
char    *node;       /* Network node name, stored in parm */
char    *task   = 0; /* Optional additional parameter appended to parm */
char    *p;
short    c, s;
long    status = 0;
char    str[40];

        if ( VME_IO != pbo->out.type ) {
		status = S_dev_badBus;
		goto bail;
	}

        c = pbo->out.value.vmeio.card;
        s = pbo->out.value.vmeio.signal;

        if ( c < 0 )
		status = S_dev_badCard;

        if ( s < 0 )
		status = S_dev_badSignal;

        if ( ! (recPvt = calloc( 1, sizeof( *recPvt ) )) ) {
		sprintf( str, "No memory for recPvt structure");
		status = S_rec_outMem;
		goto bail;
        }

        /* Break parm into node name and optional parameter */
        node = strtok( pbo->out.value.vmeio.parm, "+" );
        if ( (p = strtok( NULL, "+")) ) {
                task = p;
                if ( strcmp( task, "RESET") ) {
			sprintf( str, "Unknown task parameter %s", task);
			status = S_dev_badSignal;
		}
        }

        /* Find data structure in registry by MCH name */
        if ( (mch = devMchFind(node)) ) {
                recPvt->mch  = mch;
                recPvt->task = task;
                pbo->dpvt  = recPvt;                
        }
        else {
		sprintf( str, "Failed to locate data structure" );
		status = S_dev_noDeviceFound;
        }

bail:
	if ( status ) {
	       recGblRecordError( status, (void *)pbo , (const char *)str );
	       pbo->pact=TRUE;
	}

        return status;

}

static long write_bo(struct boRecord *pbo)
{
uint8_t *data   = malloc(MSG_MAX_LENGTH);
MchRec   recPvt = pbo->dpvt;
MchDev   mch;
MchData  mchData;
char    *task;
long     status = NO_CONVERT;

	if ( !recPvt )
		return status;

	mch     = recPvt->mch;
	mchData = mch->udata;
	task    = recPvt->task;

	if ( mchIsAlive[mchData->instance] ) {

		if ( !strcmp( task, "RESET" ) ) {
			epicsMutexLock( mch->mutex );
		       	ipmiMsgColdReset( mchData, data );
			epicsMutexUnlock( mch->mutex );
		}

		pbo->udf = FALSE;
		return status;
	}
	else {
		recGblSetSevr( pbo, WRITE_ALARM, INVALID_ALARM );
		return ERROR;
	}
}

static long
init_mbbi(struct mbbiRecord *pmbbi)
{
	scanIoInit( &drvMchStatScan );
	return 0;
}

/*
** Add this record to our IOSCANPVT list.
*/
static long 
mbbi_ioint_info(int cmd, struct mbbiRecord *pmbbi, IOSCANPVT *iopvt)
{
       *iopvt = drvMchStatScan;
       return 0;
} 

static long 
init_mbbi_record(struct mbbiRecord *pmbbi)
{
MchRec  recPvt  = 0; /* Info stored with record */
MchDev  mch     = 0; /* MCH device data structures */
char    *node;       /* Network node name, stored in parm */
char    *task   = 0; /* Optional additional parameter appended to parm */
char    *p;
short    c, s;
long     status = 0;
char     str[40];

        if ( VME_IO != pmbbi->inp.type ) {
		status = S_dev_badBus;
		goto bail;
	}

        c = pmbbi->inp.value.vmeio.card;
        s = pmbbi->inp.value.vmeio.signal;

        if ( c < 0 )
		status = S_dev_badCard;

        if ( s < 0 )
		status = S_dev_badSignal;

        if ( ! (recPvt = calloc( 1, sizeof( *recPvt ) )) ) {
		sprintf( str, "No memory for recPvt structure");
		status = S_rec_outMem;
		goto bail;
        }

        /* Break parm into node name and optional parameter */
        node = strtok( pmbbi->inp.value.vmeio.parm, "+" );
        if ( (p = strtok( NULL, "+")) ) {
                task = p;
                if ( strcmp( task, "STAT" ) && strcmp( task, "HS") ) {
			sprintf( str, "Unknown task parameter %s", task);
			status = S_dev_badSignal;
		}
        }

        /* Find data structure in registry by MCH name */
        if ( (mch = devMchFind(node)) ) {
                recPvt->mch  = mch;
                recPvt->task = task;
                pmbbi->dpvt  = recPvt;
                
        }
        else {
		sprintf( str, "Failed to locate data structure" );
		status = S_dev_noDeviceFound;
        }

bail:
	if ( status ) {
	       recGblRecordError( status, (void *)pmbbi , (const char *)str );
	       pmbbi->pact=TRUE;
	}

	return status;
}

static long 
read_mbbi(struct mbbiRecord *pmbbi)
{
uint8_t *data   = malloc(MSG_MAX_LENGTH);
MchRec   recPvt = pmbbi->dpvt;
MchDev   mch;
MchData  mchData;
char    *task;
uint8_t  value = 0, sensor;
uint16_t addr = 0; /* get addr */
short    index  = pmbbi->inp.value.vmeio.signal; /* Sensor index */
long     status = NO_CONVERT;

	if ( !recPvt )
		return status;

	mch     = recPvt->mch;
	mchData = mch->udata;
	task    = recPvt->task;

	if ( task ) {
		if ( !strcmp( task, "STAT" ) )
			pmbbi->val = mchIsAlive[mchData->instance];

		else if ( !strcmp( task, "HS") ) {

			if ( mchIsAlive[mchData->instance] ) {
				sensor = mchData->sens[index].sdr.number;

				epicsMutexLock( mch->mutex );

				ipmiMsgReadSensor( mchData, data, sensor, addr );
	
				/* Store raw sensor reading */
				mchData->sens[index].val = value;

				epicsMutexUnlock( mch->mutex );

				value = data[IPMI_RPLY_HS_SENSOR_READING_OFFSET];
				pmbbi->rval = value;
				pmbbi->val  = value;
			}
		}	
	}

	pmbbi->udf = FALSE;
	return status;
}

/* Create the dset for mbbo support */

static long 
init_mbbo_record(struct mbboRecord *pmbbo)
{
MchRec  recPvt  = 0; /* Info stored with record */
MchDev  mch     = 0; /* MCH device data structures */
char    *node;       /* Network node name, stored in parm */
char    *task   = 0; /* Optional additional parameter appended to parm */
char    *p;
short    c, s;
long     status = 0;
char     str[40];

        if ( VME_IO != pmbbo->out.type ) {
		status = S_dev_badBus;
		goto bail;
	}

        c = pmbbo->out.value.vmeio.card;
        s = pmbbo->out.value.vmeio.signal;

        if ( c < 0 )
		status = S_dev_badCard;

        if ( s < 0 )
		status = S_dev_badSignal;

        if ( ! (recPvt = calloc( 1, sizeof( *recPvt ) )) ) {
		sprintf( str, "No memory for recPvt structure");
		status = S_rec_outMem;
		goto bail;
        }

        /* Break parm into node name and optional parameter */
        node = strtok( pmbbo->out.value.vmeio.parm, "+" );
        if ( (p = strtok( NULL, "+")) ) {
                task = p;
                if ( strcmp( task, "CHAS" ) && strcmp( task, "FRU") ) { 
			sprintf( str, "Unknown task parameter %s", task);
			status = S_dev_badSignal;
		}
        }

        /* Find data structure in registry by MCH name */
        if ( (mch = devMchFind(node)) ) {
                recPvt->mch  = mch;
                recPvt->task = task;
                pmbbo->dpvt  = recPvt;
        }
        else {
		sprintf( str, "Failed to locate data structure" );
		status = S_dev_noDeviceFound;
        }

bail:
	if ( status ) {
	       recGblRecordError( status, (void *)pmbbo , (const char *)str );
	       pmbbo->pact=TRUE;
	}

	return status;
}

/* if no task, set error */
static long 
write_mbbo(struct mbboRecord *pmbbo)
{
uint8_t *data   = malloc(MSG_MAX_LENGTH);
MchRec   recPvt = pmbbo->dpvt;
MchDev   mch;
MchData  mchData;
char    *task;
long     status = NO_CONVERT;
int      cmd;
int      id     = pmbbo->out.value.vmeio.signal;
int      index, i = 0;
volatile uint8_t val;

	if ( !recPvt )
		return status;

	mch     = recPvt->mch;
	mchData = mch->udata;
	task    = recPvt->task;

	if ( mchIsAlive[mchData->instance] ) {

		if ( task ) {
			if ( !strcmp( task, "CHAS" ) ) {
				epicsMutexLock( mch->mutex );
				ipmiMsgChassisControl( mchData, data, pmbbo->val );
				epicsMutexUnlock( mch->mutex );
			}
			else if ( !strcmp( task, "FRU" ) ) {

				if ( pmbbo->val > 1 ) { /* reset not supported yet */
					recGblSetSevr( pmbbo, STATE_ALARM, MAJOR_ALARM );
					return ERROR;
				}

				cmd = ( pmbbo->val == 2 ) ? 0 : pmbbo->val; 

				epicsMutexLock( mch->mutex );

				ipmiMsgSetFruActPolicyHelper( mchData, data, id, cmd );

				epicsMutexUnlock( mch->mutex );
		      
				if ( pmbbo->val == 2 ) {

					index = mchData->fru[id].hotswap;
					val   = (volatile uint8_t)mchData->sens[index].val;

					while( val != 2 ) {
						epicsThreadSleep(1);
						i++;
						if (i > RESET_TIMEOUT ) {
							recGblSetSevr( pmbbo, TIMEOUT_ALARM, MAJOR_ALARM );
							return ERROR; /* also set FRU status */
						}
					}

					epicsMutexLock( mch->mutex );
    
					ipmiMsgSetFruActPolicyHelper( mchData, data, id, 1 );

					epicsMutexUnlock( mch->mutex );
				 }	    
			}
		}
		pmbbo->udf = FALSE;
		return status;
	}
	else {
		recGblSetSevr( pmbbo, WRITE_ALARM, INVALID_ALARM );
		return ERROR;
	}
}

/* 
 * Device support to read FRU data
 */

static long 
init_fru_ai_record(struct aiRecord *pai)
{
MchRec  recPvt  = 0; /* Info stored with record */
MchDev  mch     = 0; /* MCH device data structures */
char   *node;        /* Network node name, stored in parm */
char   *task    = 0; /* Optional additional parameter appended to parm */
char   *p;
short   c, s;
long    status = 0;
char    str[40];

        if ( VME_IO != pai->inp.type ) {
		status = S_dev_badBus;
		goto bail;
	}

        c = pai->inp.value.vmeio.card;
        s = pai->inp.value.vmeio.signal;

        if ( c < 0 )
		status = S_dev_badCard;

        if ( s < 0 )
		status = S_dev_badSignal;

	if ( ! (recPvt = calloc( 1, sizeof( *recPvt ) )) ) {
		sprintf( str, "No memory for recPvt structure");
		status = S_rec_outMem;
		goto bail;
	}

	/* Break parm into node name and optional parameter */
	node = strtok( pai->inp.value.vmeio.parm, "+" );
	if ( (p = strtok( NULL, "+" )) ) {
		task = p;
		if ( strcmp( task, "TYPE" ) && strcmp( task, "BSN" ) && strcmp( task, "PSN" ) ) {
			sprintf( str, "Unknown task parameter %s", task);
			status = S_dev_badSignal;
		}
	}

	/* Find data structure in registry by MCH name */
	if ( (mch = devMchFind(node)) ) {
		recPvt->mch  = mch;
                recPvt->task = task;
		pai->dpvt    = recPvt;                
	}
	else {
		sprintf( str, "Failed to locate data structure" );
		status = S_dev_noDeviceFound;
        }

bail:
	if ( status ) {
	       recGblRecordError( status, (void *)pai , (const char *)str );
	       pai->pact=TRUE;
	}

	return status;
}

static long read_fru_ai(struct aiRecord *pai)
{
MchRec   recPvt  = pai->dpvt;
MchDev   mch;
MchData  mchData;
char    *task;
int      id   = pai->inp.value.vmeio.signal; /* FRU ID */
long     status = NO_CONVERT;
Fru      fru;

	if ( !recPvt )
		return status;

	mch     = recPvt->mch;
	mchData = mch->udata;
	task    = recPvt->task;
	fru     = &mchData->fru[id];

	if ( mchInitDone[mchData->instance] ) {

		if ( !strcmp( task, "BSN" ) && fru->board.sn.data )
       			pai->val = (epicsFloat64)(*fru->board.sn.data);

		if ( !strcmp( task, "PSN" ) && fru->prod.sn.data )
       			pai->val = (epicsFloat64)(*fru->prod.sn.data);

			pai->udf = FALSE;

#ifdef DEBUG
printf("read_fru_ai: %s FRU id is %i, value is %.0f\n",pai->name, id, pai->val);
#endif
		pai->udf = FALSE;
		return status;
	}
	else {
		recGblSetSevr( pai, READ_ALARM, INVALID_ALARM );
		return ERROR;
	}
}

/*
** Add this record to our IOSCANPVT list.

static long 
ai_fru_ioint_info(int cmd, struct aiRecord *pai, IOSCANPVT *iopvt)
{
       *iopvt = drvMchFruScan;
       return 0;
} 
*/

/* Create the dset for stringin support */
static long 
init_fru_stringin_record(struct stringinRecord *pstringin)
{
MchRec  recPvt  = 0; /* Info stored with record */
MchDev  mch     = 0; /* MCH device data structures */
char    *node;       /* Network node name, stored in parm */
char    *task   = 0; /* Optional additional parameter appended to parm */
char    *p;
short    c, s; 
long    status  = 0;
char    str[40];

        if ( VME_IO != pstringin->inp.type ) {
		status = S_dev_badBus;
		goto bail;
        }

        c = pstringin->inp.value.vmeio.card;
        s = pstringin->inp.value.vmeio.signal;

        if ( c < 0 )
		status = S_dev_badCard;

        if ( s < 0 )
		status = S_dev_badSignal;

	if ( ! (recPvt = calloc( 1, sizeof( *recPvt ) )) ) {
		sprintf( str, "No memory for recPvt structure");
		status = S_rec_outMem;
		goto bail;
	}

	/* Break parm into node name and optional parameter */
	node = strtok( pstringin->inp.value.vmeio.parm, "+" );
	if ( (p = strtok( NULL, "+" )) ) {
		task = p;
		if ( strcmp( task, "BMF" ) && strcmp( task, "BP" ) && strcmp( task, "PMF" ) && strcmp( task, "PP") && strcmp( task, "BPN" ) && strcmp( task, "PPN") ) {
			sprintf( str, "Unknown task parameter %s", task);
			status = S_dev_badSignal;
		}
	}

	/* Find data structure in registry by MCH name */
	if ( (mch = devMchFind(node)) ) {
		recPvt->mch  = mch;
                recPvt->task = task;
		pstringin->dpvt    = recPvt;
                
	}
	else {
		sprintf( str, "Failed to locate data structure" );
		status = S_dev_noDeviceFound;
        }

bail:
	if ( status ) {
	       recGblRecordError( status, (void *)pstringin , (const char *)str );
	       pstringin->pact=TRUE;
	}

	return status;
}

static long read_fru_stringin(struct stringinRecord *pstringin)
{
MchRec   recPvt  = pstringin->dpvt;
MchDev   mch;
MchData  mchData;
char    *task;
short    id   = pstringin->inp.value.vmeio.signal;   /* FRU ID */
int      i;
long     status = NO_CONVERT;
Fru      fru;

	if ( !recPvt )
		return status;

	mch     = recPvt->mch;
	mchData = mch->udata;
	task    = recPvt->task;
	fru     = &mchData->fru[id];

	if ( mchInitDone[mchData->instance] ) {

		if ( !strcmp( task, "BMF" ) && fru->board.manuf.data )
			for ( i = 0; i < fru->board.manuf.length; i++ )
				pstringin->val[i] = fru->board.manuf.data[i];

		if ( !strcmp( task, "BP" ) && fru->board.prod.data )
			for ( i = 0; i < fru->board.prod.length; i++ )
				pstringin->val[i] = fru->board.prod.data[i];

		if ( !strcmp( task, "PMF" ) && fru->prod.manuf.data )
			for ( i = 0; i < fru->prod.manuf.length; i++ )
				pstringin->val[i] = fru->prod.manuf.data[i];

		if ( !strcmp( task, "PP" ) && fru->prod.prod.data )
			for ( i = 0; i < fru->prod.prod.length; i++ )
				pstringin->val[i] = fru->prod.prod.data[i];

		if ( !strcmp( task, "BPN" ) && fru->board.part.data )
			for ( i = 0; i < fru->board.part.length; i++ )
				pstringin->val[i] = fru->board.part.data[i];

		if ( !strcmp( task, "PPN" ) && fru->prod.part.data )
			for ( i = 0; i < fru->prod.part.length; i++ )
				pstringin->val[i] = fru->prod.part.data[i];

#ifdef DEBUG
errlogPrintf("\nread_fru_stringin: %s, task is %s, card is %i, id into FRU array is %i\n", pstringin->name, task, pstringin->inp.value.vmeio.card, pstringin->inp.value.vmeio.signal);
#endif
		pstringin->udf = FALSE;
		return status;
	}
	else {
		recGblSetSevr( pstringin, READ_ALARM, INVALID_ALARM );
		return ERROR;
	}
}

/*
** Add this record to our IOSCANPVT list.

static long 
stringin_fru_ioint_info(int cmd, struct stringinRecord *pstringin, IOSCANPVT *iopvt)
{
       *iopvt = drvMchFruScan;
       return 0;
} 
*/

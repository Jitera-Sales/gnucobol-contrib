/*
    Copyright (C) 2016-2024 Sauro Menna
 *
 *	This file is part of GCSORT.
 *
 *  GCSORT is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GCSORT is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GCSORT.  If not, see <http://www.gnu.org/licenses/>.

*/
 
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "job.h"
#include "file.h"
#include "gcthread.h"
#include "gcsort.h"
#include "sumfield.h"
#include "outrec.h"
#include "gcshare.h"
#include "sortfield.h"

#ifdef _THREAD_LINUX_ENV
	#include <pthread.h>
#endif 

GCThread struct pParam* pParThread;
GCThread struct job_t* pJob ;


extern int gcMultiThreadIsFirst;

#if defined(_THREAD_LINUX_ENV)
	pthread_mutex_t main_thread_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif 


int gcthread_start(int argc, char** argv, int nMaxThread, time_t* timeStart)
{
#ifdef  _THREAD_WIN_ENV_
	HANDLE hThread[MAX_THREAD];
#else
	pthread_t thread[MAX_THREAD];
	pthread_attr_t attr;
	int rc, status;
	/* Initialize and set thread detached attribute */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
#endif

	int rtc = 0;
	struct job_t* jArJob[MAX_THREAD];
	struct pParam* pParThread[MAX_THREAD];

	int nThread = nMaxThread;  /* job_nThread*/
	int nSkipRec = 0;
	int nStopAft = 0;
	int nBlock=0;
	int nRC = 0;

	fprintf(stdout, " ------------------------------- MultiThread [%d Threads]------------------------------- \n", nThread);

	nBlock = 0;
	for (int t = 0; t < nThread; t++) {
		jArJob[t] = gcthread_CreateJob(argc, argv, &nRC);
		/* Check requirements for Thread */

		if (nRC != 0)		/* Error */
			return -1;
		gcMultiThreadIsFirst = 1;	/* set 1 to print first time information about statistic */
		jArJob[t]->nMultiThread = 1;

		if (t == 0)   /* only first time print output*/
			nRC = job_print(jArJob[t]);
		nRC = job_check(jArJob[t]);
		if (nRC != 0)
			return nRC;

		nSkipRec = t * nBlock + nBlock;
		struct pParam* pPar = (struct pParam*)malloc(sizeof(struct pParam));
		pParThread[t] = pPar;
		pPar->thJob = jArJob[t];
		pPar->nRet = 0;
		jArJob[t]->nCurrThread = t+1;	/* Save Current Thread */
		jArJob[t]->nMaxThread = nThread;


		/* Attenzione delete dei campi allocati !"!"! */
		job_AllocateField(jArJob[t]);


#ifdef  _THREAD_WIN_ENV_
		hThread[t] = (HANDLE)_beginthread(&gcthread_run, 0, (void*)pPar);
		if (hThread[t] == NULL)
		{
			printf("ERROR; return code from _beginthread() is %p\n", hThread[t]);
			exit(-1);
		}
#else
		rc = pthread_create(&thread[t], &attr, gcthread_run, (void*)pPar);
		if (rc)
		{
			printf("ERROR; return code from pthread_create() is %d\n", rc);
			exit(-1);
		}
#endif

		/* Wait n second between loops. */
#ifdef  _THREAD_WIN_ENV_
		Sleep(500L);
#else
		sleep(1); /* 1 second */
#endif
	}

#ifdef  _THREAD_WIN_ENV_
	DWORD dwRet = WaitForMultipleObjects(nThread, hThread, TRUE, INFINITE);
	if (dwRet != 0)
		fprintf(stdout, "*GCSort*gcthread*S001* ERROR in WaitForMultipleObjects  - Return code %lu\n", dwRet);

	#else
for (int s = 0; s < nThread; s++)
{
	rc = pthread_join(thread[s], (void**)&status);
	if (rc)
	{
		printf("ERROR return code from pthread_join() is %d\n", rc);
		exit(-1);
	}
	/* printf("Completed join with thread %d status= %d\n", s, status); */
}
#endif

/* Merge all temporary files */

	if (jArJob[0]->nStatistics == 2)
		util_print_time_elap("Thread   - Before  Thread job_save_final    ");

	nRC = gcthread_save_final(jArJob);

	if (jArJob[0]->nStatistics == 2)
		util_print_time_elap("Thread   - After   Thread job_save_final    ");

	nRC = gcthread_print_final(jArJob, timeStart);

	cob_tidy();

/* Destroy the thread object. */
	for (int t = 0; t < nThread; t++)
	{

		job_destroy(jArJob[t]);
		job_destructor(jArJob[t]);

		if (pParThread[t] != NULL)
			free(pParThread[t]);
	}

	return rtc;
}


#ifdef  _THREAD_WIN_ENV_
	void gcthread_run(void* pArguments)
#else
	void* gcthread_run(void* pArguments)
#endif
{

	pParThread = (struct pParam*)pArguments;
	pJob = pParThread->thJob;

	int nContinueSrtTmp = 0;
	int nRC = 0;
	pJob->recordReadInCurrent = 0;

	do {
		if (pJob->nStatistics == 2)
			util_print_time_elap_thread("Before  Thread job_loadFiles     ", pParThread->thJob->nCurrThread);
		nContinueSrtTmp = 0;
		nRC = job_loadFiles(pJob);
		if (pJob->nStatistics == 2)
			util_print_time_elap_thread("After   Thread job_loadFiles     ", pParThread->thJob->nCurrThread);
		 if (nRC == -2)
			nContinueSrtTmp = 1;
		if (nRC == -1)
			break;
		/* Sort data */
		if (pJob->nStatistics == 2)
			util_print_time_elap_thread("Before  Thread job_sort          ", pParThread->thJob->nCurrThread);

#ifdef _THREAD_LINUX_ENV
		pthread_mutex_lock(&main_thread_mutex);
#endif
		nRC = job_sort_data(pJob);

#ifdef _THREAD_LINUX_ENV
	 	pthread_mutex_unlock(&main_thread_mutex);
#endif
		if (pJob->nStatistics == 2)
			util_print_time_elap_thread("After   Thread job_sort          ", pParThread->thJob->nCurrThread);
		if (nRC == -1)
			break;
		if (pJob->bIsPresentSegmentation == 1) {
			if (pJob->nStatistics == 2)
				util_print_time_elap_thread("Before  Thread job_save_temp     ", pParThread->thJob->nCurrThread);
			nRC = job_save_tempfile(pJob);
			if (pJob->nStatistics == 2)
				util_print_time_elap_thread("After   Thread job_save_temp     ", pParThread->thJob->nCurrThread);
			if (nRC == -1)
				break;
	 	}
	} while (nContinueSrtTmp == 1);


	pParThread->nRet = nRC;

#ifdef  _THREAD_WIN_ENV_
	_endthread();
#else
/*	pthread_exit((void*)0); */
	pthread_exit((void*)NULL);
#endif
}

struct job_t* gcthread_CreateJob(int argc, char** argv, int* nRC)
{
	struct job_t* pJ = job_constructor();
	*nRC = job_load(pJ, argc, argv);
	if (*nRC != 0)
		*nRC = -1;
	return pJ;
}

int gcthreadSkipStop(struct job_t* job)
{
	/* nMultiThread; */		/* flag MultiThread 0=no, 1=yes*/
	/* nCurrThread;	 */		/* current Thread */
	/* nMaxThread;	 */		/* num max Thread */

	int nNumRecords = job->inputFile->nFileMaxSize / job->inputLength;
	int nNumRecordForBlock = nNumRecords / job->nMaxThread;

	job->nMTSkipRec = ((job->nCurrThread-1) * nNumRecordForBlock);
	job->nMTStopAft = ((job->nCurrThread-1) * nNumRecordForBlock) + nNumRecordForBlock;

	/* only for test   
	job->nSkipRec = (job->nCurrThread * 0);
	job->nStopAft = (job->nCurrThread * 0) + nNumRecordForBlock;
	  */
	if (job->nCurrThread == job->nMaxThread)
		job->nMTStopAft = 0;

#ifdef GCSTHREAD
	fprintf(stdout,"Thread : %d, MT SkipRec: %ld, MT StopAft: %ld\n", job->nCurrThread, job->nMTSkipRec, job->nMTStopAft);
#endif
	return 0;
}


/* gcthread_save_final   */
/* Merge all temporary files */
int gcthread_save_final(struct job_t* jobArray[]) {

	struct job_t* job = jobArray[0];	/* First element */

	char szNameTmp[FILENAME_MAX];
	int	bIsEof[MAX_HANDLE_THREAD];
	int	bIsFirstSumFields = 0;
	int	handleTmpFile[MAX_HANDLE_THREAD];
	int	iSeek = 0;
	int	nMaxEle = 0;

	int	nSplitPosPnt = SZLENREC;
	int	nSumEof;
	int bFirstRound = 0;
	int bIsFirstTime = 0;
	int bIsWrited = 0;
	int bTempEof = 0;
	int byteRead = 0;
	int byteReadTemp = 0;
	int byteReadTmpFile[MAX_HANDLE_THREAD];
	int desc = 0;
	int kj = 0;
	int nCompare = 1;
	int nIdx1, nIdx2, k;
	int nLastRead = 0;
	int nLenRekTemp = 0;
	int nNumBytes = 0;
	int nNumBytesTemp = 0;
	int nPosPtr;
	int p = 0;
	int posAr = 0;
	int position_buf_read = 0;
	int previousRecord = -1;
	int recordBufferLength;
	int retcode_func = 0;
	int useRecord;
	struct mmfio_t* ArrayFile[MAX_HANDLE_THREAD];
	unsigned char  szKeyCurr[GCSORT_KEY_MAX + SZPOSPNT];
	unsigned char  szKeyPrec[GCSORT_KEY_MAX + SZPOSPNT];
	unsigned char  szKeySave[GCSORT_KEY_MAX + SZPOSPNT];
	unsigned char  szKeyTemp[GCSORT_KEY_MAX + SZPOSPNT];
	unsigned char* ptrBuf[MAX_HANDLE_THREAD];
	unsigned char* recordBuffer;
	unsigned char* recordBufferPrevious;  /* for Sum Fileds NONE    */
	unsigned char* szBufRekTmpFile[MAX_HANDLE_THREAD];
	unsigned char* szBuffRek;
	unsigned char* szBuffRekOutRec;
	unsigned char* szPrecSumFields;	/* Prec */
	unsigned char* szSaveSumFields; /* save */
	unsigned char* szFirstRek;
	unsigned int   nLenRek = 0;
	unsigned int   nLenPrec = 0;
	unsigned int   nLenRecOut = 0;
	unsigned int   nLenSave = 0;
	unsigned int   bIsFirstKeySumField = 0;
	int nTypeSource = 0;        /* 0 = Memory, 1 = File Temp (MMF) */

	int nPosIdArray = -1;
	int nCountEle = 0;


	if (job->bIsPresentSegmentation == 0)
		nTypeSource = 0;
	else
		nTypeSource = 1;

	recordBufferLength = MAX_RECSIZE;   /*    (job->outputLength>job->inputLength?job->outputLength:job->inputLength);    */
	
	recordBufferLength = SIZEINT + recordBufferLength + SZPOSPNT + SIZEINT + NUMCHAREOL;
	
	recordBuffer = (unsigned char*)malloc(recordBufferLength);
	if (recordBuffer == 0)
		fprintf(stdout, "*GCSORT*S048*ERROR: Cannot Allocate recordBuffer : %s\n", strerror(errno));
	recordBufferPrevious = (unsigned char*)malloc(recordBufferLength);
	if (recordBuffer == 0)
		fprintf(stdout, "*GCSORT*S049*ERROR: Cannot Allocate recordBuffer : %s\n", strerror(errno));

	for (kj = 0; kj < MAX_HANDLE_THREAD; kj++) {
		szBufRekTmpFile[kj] = (unsigned char*)malloc(recordBufferLength);
		if (szBufRekTmpFile[kj] == 0)
			fprintf(stdout, "*GCSORT*S050*ERROR: Cannot Allocate szBufRek1 : %s - id : %d\n", strerror(errno), kj);
	}
	szBuffRek = (unsigned char*)malloc(recordBufferLength);
	if (szBuffRek == 0)
		fprintf(stdout, "*GCSORT*S051*ERROR: Cannot Allocate szBuffRek : %s\n", strerror(errno));
	szBuffRekOutRec = (unsigned char*)malloc(recordBufferLength);
	if (szBuffRekOutRec == 0)
		fprintf(stdout, "*GCSORT*S052*ERROR: Cannot Allocate szBuffRekOutRec : %s\n", strerror(errno));
	szPrecSumFields = (unsigned char*)malloc(recordBufferLength);
	if (szPrecSumFields == 0)
		fprintf(stdout, "*GCSORT*S053*ERROR: Cannot Allocate szPrecSumFields : %s\n", strerror(errno));
	szSaveSumFields = (unsigned char*)malloc(recordBufferLength);
	if (szSaveSumFields == 0)
		fprintf(stdout, "*GCSORT*S054*ERROR: Cannot Allocate szSaveSumFields : %s\n", strerror(errno));
	szFirstRek = (unsigned char*)malloc(recordBufferLength);
	if (szFirstRek == 0)
		fprintf(stdout, "*GCSORT*S054A*ERROR: Cannot Allocate szFirstRek : %s\n", strerror(errno));

	/* new
	   Verify segmentation and if last section of file input
	*/

	cob_open(job->outputFile->stFileDef, COB_OPEN_OUTPUT, 0, NULL);
	if (atol((char*)job->outputFile->stFileDef->file_status) != 0) {
		fprintf(stdout, "*GCSORT*S055*ERROR: Cannot open file %s - File Status (%c%c)\n", file_getName(job->outputFile),
			job->outputFile->stFileDef->file_status[0], job->outputFile->stFileDef->file_status[1]);
		retcode_func = -1;
		goto gcthread_save_tempfinal_exit;
	}

	if (job->outfil != NULL) {
		if (outfil_open_files(job) < 0) {
			retcode_func = -1;
			goto gcthread_save_tempfinal_exit;
		}
	}
	/*  -->> debug 	printf("=======================================Write Final \n");    */
	/* individuazione dei file da aprire */
	nIdx1 = job->nIndextmp;
	nIdx2 = job->nIndextmp2;

	for (kj = 0; kj < MAX_HANDLE_THREAD; kj++) {
		byteReadTmpFile[kj] = 0;
		handleTmpFile[kj] = 0;
		bIsEof[kj] = 1;
	}
	nLastRead = 0;

	/* Open files Tmp   */
	int j = -1;
	if (nTypeSource == 1) {		/* 0 = Memory, 1 = File Temp (MMF) */
		/* Max Thread*/
		for (int s = 0; s < job->nMaxThread; s++) {
			job = jobArray[s];
			/* for (k = 0; k < MAX_HANDLE_TEMPFILE; k++) */
			for (k = 0; k < MAX_HANDLE_TEMPFILE; k++)
			{
				if (job->nCountSrt[k] == 0)
					continue;
				j++;
				bIsEof[j] = 0;
				strcpy(szNameTmp, job->array_FileTmpName[k]);
				ArrayFile[j] = mmfio_constructor();
				if (mmfio_Open((const unsigned char*)szNameTmp, OPN_READ, 0, ArrayFile[j]) == 0) {
					fprintf(stdout, "*GCSORT*S056*ERROR: Cannot open file %s : %s\n", szNameTmp, strerror(errno));
					retcode_func = -1;
					goto gcthread_save_tempfinal_exit;
				}
				handleTmpFile[j] = (int)ArrayFile[j]->m_hFile;
			}
		}
	}
	else
	{
		/* Max Thread*/
		j = -1;
		for (int s = 0; s < job->nMaxThread; s++) {
			job = jobArray[s];
			job->recordReadInCurrent = 0; /* reset counter for read */
			if (job->recordNumber == 0)
					continue;
			j++;
			bIsEof[j] = 0;
			handleTmpFile[j] = 1;
		}
	}

	/* in this point j is max num */
	/**/
#ifdef GCSTHREAD
	fprintf(stdout, "--------------------------------DEBUG DEBUG DEBUG Counter save temp file --- \n");
	for (int s = 0; s < job->nMaxThread; s++) {
		job = jobArray[s];
		fprintf(stdout, "--------------------------------Counter save temp file %d --- \n", s+1);
		fprintf(stdout, "jobArray[%d]\n", s);
		for (k = 0; k < MAX_HANDLE_TEMPFILE; k++) {
			if (job->nCountSrt[k] > 0) {
				fprintf(stdout, "job->nCountSrt[%d] = %d\n", k,  job->nCountSrt[k]);
			}
		}
	}

	fprintf(stdout, "--------------------------------DEBUG DEBUG DEBUG Counter save temp file --- \n");
	/* */
#endif

	/* save num of temporary files */
	int nTempFile = j;
	/* First element */
	job = jobArray[0];
	/* reset counters */
	job->recordWriteOutTotal = 0;

	for (kj = 0; kj < MAX_HANDLE_THREAD; kj++) {
		if (handleTmpFile[kj] != 0)
			ptrBuf[kj] = (unsigned char*)szBufRekTmpFile[kj];
		else
			ptrBuf[kj] = 0x00;
	}
	bFirstRound = 1;
	nSumEof = 0;
	bIsFirstTime = 1;
	if (nTypeSource == 1) {
		for (kj = 0; kj < MAX_HANDLE_THREAD; kj++) {
			if (bIsEof[kj] == 0) {
				bIsEof[kj] = job_ReadFileTemp(ArrayFile[kj], &byteReadTmpFile[kj], szBufRekTmpFile[kj], bIsFirstTime);  /* bIsEof = 0 ok, 1 = eof   */
				if (bIsEof[kj] == 1) {
					ptrBuf[kj] = 0x00;
				}
			}
			nSumEof = nSumEof + bIsEof[kj];
		}
	}
	else
	{
		for (kj = 0; kj < MAX_HANDLE_THREAD; kj++) {
			if (bIsEof[kj] == 0) {
				bIsEof[kj] = gcthread_ReadFileMem(jobArray[kj], &byteReadTmpFile[kj], szBufRekTmpFile[kj], bIsFirstTime);  /* bIsEof = 0 ok, 1 = eof   */
				if (bIsEof[kj] == 1) {
					ptrBuf[kj] = 0x00;
				}
			}
			nSumEof = nSumEof + bIsEof[kj];
		}
	}
	bFirstRound = 0;
	bIsFirstTime = 0;

	if (nTypeSource == 1) {
		nMaxEle = j+1;
		job->nNumTmpFile = j+1;
	}
	else
	{
		nMaxEle = job->nMaxThread;
		job->nNumTmpFile = nMaxEle;
	}

	/* s.m. 20240302 */
	globalJob = job;

	nPosPtr = job_IdentifyBuf(job, ptrBuf, nMaxEle);

	if (nPosPtr >= 0) {
		gcthread_GetKeys(job, szBufRekTmpFile[nPosPtr] + SZLENREC, szKeyTemp);
		SumField_ResetTot(job); /* reset totalizer  */
		bIsFirstSumFields = 1;
		nLenRek = byteReadTmpFile[nPosPtr];
		memmove(szKeyPrec, szBufRekTmpFile[nPosPtr], SZLENREC);
		memmove(szKeyPrec + SZLENREC, szKeyTemp, job->nLenKeys);
		memmove(szPrecSumFields, szBufRekTmpFile[nPosPtr], nLenRek + SZLENREC);
		nLenPrec = nLenRek;
		memmove(szKeySave, szKeyPrec, job->nLenKeys + SZLENREC);			   /*   lPosPnt + Key   */
		memmove(szSaveSumFields, szPrecSumFields, nLenPrec + SZLENREC);
		nLenSave = nLenPrec;
	}
	while ((nSumEof) < MAX_HANDLE_THREAD) /* job->nNumTmpFile)    */
	{
		nLenRecOut = job->outputLength;
		nPosPtr = job_IdentifyBuf(job, ptrBuf, nMaxEle);
		nLastRead = nPosPtr;
		byteRead = byteReadTmpFile[nPosPtr];
		job->LenCurrRek = byteRead;
		useRecord = 1;
		/* s.m. 20240302   performance */
		if (job->sumFields > 0) {
			/* SUMFIELD			1 = NONE    */
			if (job->sumFields == 1) {
				if (previousRecord != -1) {
					/* check equal key  */
					job->LenCurrRek = byteRead;
					if (job_compare_rek(job, recordBufferPrevious, szBufRekTmpFile[nPosPtr], 0, SZLENREC) == 0)
						useRecord = 0;
				}
				/* enable check for sum fields  */
				previousRecord = 1;
				memmove(recordBufferPrevious, szBufRekTmpFile[nPosPtr], byteRead);
			}
			/* SUMFIELD			2 = FIELDS  */
			if (job->sumFields == 2) {
				gcthread_GetKeys(job, szBufRekTmpFile[nPosPtr] + SZLENREC, szKeyTemp);
				memmove(szKeyCurr, szBufRekTmpFile[nPosPtr], SZLENREC);			    /*  lPosPnt */
				/* ? ? memmove(szKeyCurr + SZPOSPNT, szKeyTemp, job->nLenKeys + SZPOSPNT);	*/	/*  Key     */
				memmove(szKeyCurr + SZLENREC, szKeyTemp, job->nLenKeys);		/*  Key     */
				if (bIsFirstKeySumField == 0) {			/* Save first key for sum field, use this for write */
					memcpy(szFirstRek, szBuffRek, nLenRek + nSplitPosPnt);              /* PosPnt + First Record    */
					bIsFirstKeySumField = 1;
				}
				useRecord = SumFields_KeyCheck(job, &bIsWrited, szKeyPrec, &nLenPrec, szKeyCurr, &nLenRek, szKeySave, &nLenSave,
					szPrecSumFields, szSaveSumFields, szBufRekTmpFile[nPosPtr], SZLENREC);
			}

			if (useRecord == 0) {	/* skip record  */
				if (bIsEof[nLastRead] == 0) {
					if (nTypeSource == 1)
						bIsEof[nLastRead] = job_ReadFileTemp(ArrayFile[nLastRead], &byteReadTmpFile[nLastRead], szBufRekTmpFile[nLastRead], bIsFirstTime);  /* bIsEof = 0 ok, 1 = eof   */
					else
						bIsEof[nLastRead] = gcthread_ReadFileMem(jobArray[nLastRead], &byteReadTmpFile[nLastRead], szBufRekTmpFile[nLastRead], bIsFirstTime);  /* bIsEof = 0 ok, 1 = eof   */
					if (bIsEof[nLastRead] == 1) {
						nSumEof = nSumEof + bIsEof[nLastRead];
						ptrBuf[nLastRead] = 0x00;
					}
				}
				/*  XSUM */

				/* Save record - SumField discrded record XSUM */
				if (job->nXSumFilePresent > 0) {
					job_set_area(job, job->XSUMfile, recordBuffer + nSplitPosPnt, nLenRecOut, nLenRek);	/* Len output   */
					cob_write(job->XSUMfile->stFileDef, job->XSUMfile->stFileDef->record, job->XSUMfile->opt, NULL, 0);
					retcode_func = file_checkFSWrite("Write", "gcthread_save_final", job->XSUMfile, nLenRecOut, nLenRek);
					if (retcode_func == -1)
						goto gcthread_save_tempfinal_exit;
					job->recordDiscardXSUMTotal++;
				}			/*       */
				continue;
			}

			if (bIsFirstKeySumField == 1) {
				bIsFirstKeySumField = 0;
				gc_memcpy(recordBuffer, szFirstRek, nLenRek + nSplitPosPnt);
			}
		} /* end performance */
		/* OUTREC   */
		if ((useRecord == 1) && (job->outrec != NULL)) {
			/* check overlay    */
			if (job->outrec->nIsOverlay == 0) {
				memset((unsigned char*)szBuffRekOutRec, 0x20, recordBufferLength);
				nLenRek = outrec_copy(job->outrec, szBuffRekOutRec, szBufRekTmpFile[nPosPtr], job->outputLength, byteRead, file_getFormat(job->outputFile), file_GetMF(job->outputFile), job, nSplitPosPnt);
				memset(szBufRekTmpFile[nPosPtr], 0x20, recordBufferLength); /* s.m. 202101  */
				memcpy(szBufRekTmpFile[nPosPtr], szBuffRekOutRec, nLenRek + nSplitPosPnt);
				byteReadTmpFile[nPosPtr] = nLenRek;
				byteRead = nLenRek;
				nLenRecOut = nLenRek; /* for Outrec force length of record  */
			}
			else
			{		/* Overlay  */
				memset(szBuffRek, 0x20, recordBufferLength);
				memmove(szBuffRek, recordBuffer, byteRead + nSplitPosPnt);	/* s.m. 202101 copy record  */
				nLenRek = outrec_copy_overlay(job->outrec, szBuffRekOutRec, szBufRekTmpFile[nPosPtr], job->outputLength, byteRead, file_getFormat(job->outputFile), file_GetMF(job->outputFile), job, nSplitPosPnt);
				nLenRek++;
				if (nLenRek < job->outputLength)
					nLenRek = job->outputLength;
				memset(szBufRekTmpFile[nPosPtr], 0x20, recordBufferLength); /* s.m. 202101  */
				memcpy(szBufRekTmpFile[nPosPtr], szBuffRekOutRec, nLenRek + nSplitPosPnt);
				byteReadTmpFile[nPosPtr] = nLenRek;
				byteRead = nLenRek;
				nLenRecOut = nLenRek;
			}
		}


		/* NORMAL   */
		if ((useRecord == 1) && (job->outfil == NULL)) {
			/* nPosition = nPosition + 4 + byteRead;    */
			if (job->sumFields == 2) {
				bIsWrited = 1;
				SumField_SumFieldUpdateRek((unsigned char*)szBufRekTmpFile[nPosPtr] + SZLENREC);		/* Update record in memory  */
				SumField_ResetTot(job);														        /* reset totalizer          */
				SumField_SumField((unsigned char*)szPrecSumFields + SZLENREC);						/* Sum record in  memory    */
			}

			if (byteRead > 0)
			{
				job_set_area(job, job->outputFile, szBufRekTmpFile[nPosPtr] + nSplitPosPnt, nLenRecOut, byteRead);	/* Len output   */
				cob_write(job->outputFile->stFileDef, job->outputFile->stFileDef->record, job->outputFile->opt, NULL, 0);
				retcode_func = file_checkFSWrite("Write", "gcthread_save_final", job->outputFile, nLenRecOut, byteRead);
				if (retcode_func == -1)
					goto gcthread_save_tempfinal_exit;
				/* s.m. 202012  */
				if (job->sumFields == 2) {
					memcpy(szFirstRek, szPrecSumFields, nLenRek + nSplitPosPnt);
					bIsFirstKeySumField = 1;
				}
				/* s.m. 202012  */
				job->recordWriteOutTotal++;
			}
		}
		else
		{
			/* Make output for OUTFIL   */
			if ((useRecord == 1) && (job->outfil != NULL)) {
				outfil_write_buffer(job, szBufRekTmpFile[nPosPtr] + nSplitPosPnt, byteRead, szBuffRek, nSplitPosPnt, useRecord);
				job->recordWriteOutTotal++;
			}
		}
		if (bIsEof[nLastRead] == 0) {
			if (nTypeSource == 1)
				bIsEof[nLastRead] = job_ReadFileTemp(ArrayFile[nLastRead], &byteReadTmpFile[nLastRead], szBufRekTmpFile[nLastRead], bIsFirstTime);  /* bIsEof = 0 ok, 1 = eof   */
			else
				bIsEof[nLastRead] = gcthread_ReadFileMem(jobArray[nLastRead], &byteReadTmpFile[nLastRead], szBufRekTmpFile[nLastRead], bIsFirstTime);  /* bIsEof = 0 ok, 1 = eof   */
			if (bIsEof[nLastRead] == 1) {
				ptrBuf[nLastRead] = 0x00;
				nSumEof = nSumEof + bIsEof[nLastRead];
			}
		}
	}
	if ((job->sumFields == 2) && (bIsWrited == 1)) {   /* pending buffer  */
		SumField_SumFieldUpdateRek((char*)szFirstRek + SZLENREC);	/* Update record in memory      */
		memcpy(recordBuffer, szFirstRek, nLenPrec + SZLENREC);		/* Substitute record for write  */
		/* s.m. 202012  */
		nLenRek = nLenPrec;
		nLenRecOut = job->outputLength;
		job_set_area(job, job->outputFile, recordBuffer + nSplitPosPnt, nLenRecOut, byteRead); /* Len output    */
		cob_write(job->outputFile->stFileDef, job->outputFile->stFileDef->record, job->outputFile->opt, NULL, 0);
		retcode_func = file_checkFSWrite("Write", "gcthread_save_final", job->outputFile, nLenRecOut, byteRead);
		if (retcode_func == -1)
			goto gcthread_save_tempfinal_exit;
			job->recordWriteOutTotal++; 
	}
	if (nTypeSource == 1) {
		for (iSeek = 0; iSeek <= nTempFile; iSeek++) {
			if (ArrayFile[iSeek]->m_pbFile != NULL) {
				mmfio_Close(ArrayFile[iSeek]);
				mmfio_destructor(ArrayFile[iSeek]);
				free(ArrayFile[iSeek]);
			}
		}
	}
gcthread_save_tempfinal_exit:
	free(recordBuffer);
	free(recordBufferPrevious);
	free(szSaveSumFields);
	free(szPrecSumFields);
	free(szBuffRekOutRec);
	free(szBuffRek);
	free(szFirstRek);
	for (kj = 0; kj < MAX_HANDLE_THREAD; kj++) {
		if (szBufRekTmpFile[kj] != NULL)
			free(szBufRekTmpFile[kj]);
	}

	cob_close(job->outputFile->stFileDef, NULL, COB_CLOSE_NORMAL, 0);
	if (job->nXSumFilePresent > 0)
		cob_close(job->XSUMfile->stFileDef, NULL, COB_CLOSE_NORMAL, 0);
	if (desc > 0) {
		if ((close(desc)) < 0) {
			fprintf(stdout, "*GCSORT*S059*ERROR: Cannot close file %s : %s\n", file_getName(job->outputFile), strerror(errno));
			return -1;
		}
	}
	if (job->outfil != NULL) {
		if (outfil_close_files(job) < 0)
			return -1;
	}

	return retcode_func;
}


int gcthread_print_final(struct job_t* jobArray[], time_t* timeStart)
{
	char szNameTmp[FILENAME_MAX];
	time_t timeEnd;
	struct tm* timeinfoStart;
	struct tm* timeinfoEnd;
	struct outfil_t* pOutfil;
	struct file_t* file;
	int seconds;
	int hh, mm, ss, ms;
	int nIdx;
	int desc = 0;
	struct job_t* job;
	int g_retWarn = 0;

	/* Sum all caounter from temporary file statistics */
	int64_t recordNumberTotal = 0;
	int64_t recordWriteSortTotal = 0;
	int64_t recordWriteOutTotal = 0;
	int64_t recordDiscardXSUMTotal = 0;

	job = jobArray[0];

	/* Open files Tmp   */

	int j = -1;
	/* Max Thread*/
	for (int s = 0; s < job->nMaxThread; s++) {
		job = jobArray[s];
		for (nIdx = 0; nIdx < MAX_HANDLE_THREAD; nIdx++) {
			if (job->array_FileTmpHandle[nIdx] != -1) {
				strcpy(szNameTmp, job->array_FileTmpName[nIdx]);
				
				desc = remove(szNameTmp);
				if (desc != 0) {
					fprintf(stdout, "*GCSORT*W005* WARNING : Cannot remove file %s : %s\n", szNameTmp, strerror(errno));
					g_retWarn = 4;
				} 
			}
		}
	}


	for (int s = 0; s < job->nMaxThread; s++) {
		job = jobArray[s];
		recordWriteOutTotal += job->recordWriteOutTotal;
		recordNumberTotal += job->recordNumberTotal;
		recordWriteSortTotal += job->recordWriteSortTotal;
		recordDiscardXSUMTotal += job->recordDiscardXSUMTotal;

	}

	fprintf(stdout, "====================================================================\n");
	fprintf(stdout, " Total Records Number..............: " NUM_FMT_LLD "\n", (long long)recordNumberTotal);
	fprintf(stdout, " Total Records Write Sort..........: " NUM_FMT_LLD "\n", (long long)recordWriteSortTotal);
	fprintf(stdout, " Total Records Write Output........: " NUM_FMT_LLD "\n", (long long)recordWriteOutTotal);

	if (job->nXSumFilePresent > 0) {
		fprintf(stdout, " Total Records Discard XSUM Output.: " NUM_FMT_LLD "\n", (long long)recordDiscardXSUMTotal);
	}

	fprintf(stdout, "====================================================================\n");
if (job->nStatistics == 2) {
	if (job->bIsPresentSegmentation == 1) {
		if (job->nMultiThread == 1) {
			for (int s = 0; s < job->nMaxThread; s++) {
				job = jobArray[s];
				for (nIdx = 0; nIdx < MAX_HANDLE_TEMPFILE; nIdx++) {
					if (job->nCountSrt[nIdx] > 0)
						fprintf(stdout, "Thread %d - job->nCountSrt[%02d] %d\n", s+1, nIdx, job->nCountSrt[nIdx]);
				}
			} 
			fprintf(stdout, "\n");
			fprintf(stdout, "====================================================================\n");
			fprintf(stdout, " Thread - Memory size for GCSORT data.......: " NUM_FMT_LLD "\n", (long long)job->ulMemSizeAllocData);
			fprintf(stdout, " Thread - Memory size for GCSORT key........: " NUM_FMT_LLD "\n", (long long)job->ulMemSizeAllocSort);
			fprintf(stdout, " Thread - MAX_SIZE_CACHE_WRITE..............:%10d\n", MAX_SIZE_CACHE_WRITE);
			fprintf(stdout, " Thread - MAX_SIZE_CACHE_WRITE_FINAL........:%10d\n", MAX_SIZE_CACHE_WRITE_FINAL);
			fprintf(stdout, " Thread - MAX_MLTP_BYTE.....................:%10d\n", job->nMlt);
			fprintf(stdout, "====================================================================\n");
			fprintf(stdout, "\n");
		}
		else
		{
			for (nIdx = 0; nIdx < MAX_HANDLE_TEMPFILE; nIdx++) {
				if (job->nCountSrt[nIdx] > 0)
					fprintf(stdout, "job->nCountSrt[%02d] %d\n", nIdx, job->nCountSrt[nIdx]);
			}
			fprintf(stdout, "\n");
			fprintf(stdout, " Memory size for GCSORT data.......: " NUM_FMT_LLD "\n", (long long)job->ulMemSizeAllocData);
			fprintf(stdout, " Memory size for GCSORT key........: " NUM_FMT_LLD "\n", (long long)job->ulMemSizeAllocSort);
			fprintf(stdout, " MAX_SIZE_CACHE_WRITE..............:%10d\n", MAX_SIZE_CACHE_WRITE);
			fprintf(stdout, " MAX_SIZE_CACHE_WRITE_FINAL........:%10d\n", MAX_SIZE_CACHE_WRITE_FINAL);
			fprintf(stdout, " MAX_MLTP_BYTE.....................:%10d\n", job->nMlt);
			fprintf(stdout, "===============================================\n");
			fprintf(stdout, "\n");
		}
	}

}

	if (job->outfil != NULL) {
		for (pOutfil = job->outfil; pOutfil != NULL; pOutfil = outfil_getNext(pOutfil)) {
			/* if (pOutfil->isVirtualFile == 0) {	*/ /* No Virtual */
			fprintf(stdout, "OUTFIL Total Records Write     : %10d\n", pOutfil->recordWriteOutTotal);
			if ((job->pSaveOutfil != NULL) && (pOutfil != job->pSaveOutfil)) {
				for (file = pOutfil->outfil_File; file != NULL; file = file_getNext(file)) {
					fprintf(stdout, "Record Write for file : %10d - File: %s\n", file->nCountRow, file->name);
				}
			}
			/*  } */
		}
		if (job->pSaveOutfil != NULL) {
			fprintf(stdout, "Record Write for file : %10d - SAVE: %s\n", job->pSaveOutfil->recordWriteOutTotal, job->pSaveOutfil->outfil_File->name);
		}
		fprintf(stdout, "\n");
	}

	time(&timeEnd);
	timeinfoStart = localtime(timeStart); /* localtime (&timeStart);    */
	fprintf(stdout, "Start    : %s", asctime(timeinfoStart));
	timeinfoEnd = localtime(&timeEnd);
	fprintf(stdout, "End      : %s", asctime(timeinfoEnd));
	seconds = (int)difftime(timeEnd, *timeStart);
	hh = seconds / 3600;
	mm = (seconds - hh * 3600) / 60;
	ss = seconds - ((hh * 3600) + mm * 60);
	ms = seconds - ((hh * 3600) + mm * 60 + ss);
	fprintf(stdout, "Elapsed  Time %02dhh %02dmm %02dss %03dms\n\n", hh, mm, ss, ms);

	return 0;
}


void gcthread_ReviewMemAlloc(struct job_t* job)
{

	job->ulMemSizeAllocSort = GCSORT_ALLOCATE_MEMSIZE / 100 * 10;
	job->ulMemSizeAllocData = GCSORT_ALLOCATE_MEMSIZE - job->ulMemSizeAllocSort;

	char* pEnvMemSize = getenv("GCSORT_MEMSIZE");

	if (pEnvMemSize != NULL)
	{
		job->ulMemSizeAllocData = _atoi64(pEnvMemSize);
		if (job->ulMemSizeAllocData == 0) {
			job->ulMemSizeAllocSort = GCSORT_ALLOCATE_MEMSIZE / 100 * 10;
			job->ulMemSizeAllocData = GCSORT_ALLOCATE_MEMSIZE - job->ulMemSizeAllocSort;
		}
		else
		{
			job->ulMemSizeAllocSort = job->ulMemSizeAllocData / 100 * 10;
			job->ulMemSizeAllocData = job->ulMemSizeAllocData - job->ulMemSizeAllocSort;
		}
	}


	int64_t sv_alloc = job->ulMemSizeAllocData + job->ulMemSizeAllocSort;

	double  nLenKey = job_GetLenKeys(job);
	double  nLenRek = job->inputLength;
	double  nPerc = (nLenKey + SIZESRTBUFF) / nLenRek * 100;
	/* in the case where nLenRek too big or too small   */
	if (nPerc > 50)
		nPerc = 50;
	if (nPerc < 10)
		nPerc = 10;
	job->ulMemSizeAllocSort = (sv_alloc * (int64_t)nPerc) / 100;
	job->ulMemSizeAllocData = sv_alloc - job->ulMemSizeAllocSort;

	/* s.m. 202402 */
#ifdef GCSTHREAD
	printf(" job_ReviewMemAlloc - job->ulMemSizeAllocData     " NUM_FMT_LLD " \n", (long long)job->ulMemSizeAllocData);
	printf(" job_ReviewMemAlloc - job->ulMemSizeAllocSort " NUM_FMT_LLD " \n", (long long)job->ulMemSizeAllocSort);

	printf(" job_ReviewMemAlloc - nLenRek : % f\n", nLenRek);
	printf(" job_ReviewMemAlloc - nLenKey : %f\n", nLenKey);
#endif
	/* s.m. new algorithm */
	double dT1 = 0;
	double dAreaKey = 0;
	nLenKey = job_GetLenKeys(job);
	nLenRek = job->inputLength;

	dT1 = nLenRek + nLenKey + SIZESRTBUFF;
	dAreaKey = ((nLenKey + SIZESRTBUFF) / dT1) * 100;
	dAreaKey = dAreaKey + 0.5;  /* rounding up */

	job->ulMemSizeAllocSort = (sv_alloc * (int64_t)dAreaKey) / 100;
	/*  Allocate for all Thread */
	if (job->nMultiThread > 0)
		job->ulMemSizeAllocSort = job->ulMemSizeAllocSort * job->nMultiThread;

	job->ulMemSizeAllocData = sv_alloc - job->ulMemSizeAllocSort;

	if (job->ulMemSizeAllocData > job->ulMemSizeAllocSort) {
		job->ulMemSizeAllocSort = (sv_alloc * 50) / 100;
		job->ulMemSizeAllocData = sv_alloc - job->ulMemSizeAllocSort;
	}

	job->ulMemSizeAllocData = job->ulMemSizeAllocData / job->nMaxThread;
	job->ulMemSizeAllocSort = job->ulMemSizeAllocSort / job->nMaxThread;

#ifdef GCSTHREAD
	printf(" job_ReviewMemAlloc - review file_length" NUM_FMT_LLD " \n", (long long)job->file_length);
	printf(" job_ReviewMemAlloc - review job->ulMemSizeAllocData     " NUM_FMT_LLD " \n", (long long)job->ulMemSizeAllocData);
	printf(" job_ReviewMemAlloc - review job->ulMemSizeAllocSort " NUM_FMT_LLD " \n", (long long)job->ulMemSizeAllocSort);

	printf(" job_ReviewMemAlloc - nLenRek : % f\n", nLenRek);
	printf(" job_ReviewMemAlloc - nLenKey : %f\n", nLenKey);
	printf(" job_ReviewMemAlloc - nPerc : %f\n", dAreaKey);
#endif
	// }
	/* s.m. 202402 */

}
INLINE int gcthread_ReadFileMem(struct job_t* job, int* nLR, unsigned char* szBuffRek, int nFirst)
{
	unsigned int lenBE = 0;
	int bTempEof = 0;
	if (job->recordReadInCurrent < job->recordNumber) {
		/* check data from job_ReadFileTemp */
		/* Structure 
		LenRek		4 byte
		Data Area	n byte
		POSPNT		8 byte
		(NO) ID			4 byte
		*/
		gc_memcpy(job->phSrt, job->buffertSort + (job->recordReadInCurrent) * ((int64_t)job->nLenKeys + SIZESRTBUFF) + job->nLenKeys, SIZESRTBUFF); /* Pointer Data Area    */

		/* LenRek 4 byte */
		gc_memcpy(szBuffRek, (unsigned char*)job->phSrt->pAddress, job->phSrt->nLenRek); /* buffer           */
		gc_memcpy(szBuffRek+SZLENREC, job->phSrt->pAddress, job->phSrt->nLenRek);		/* buffer   */

		job->recordReadInCurrent++;
	}
	else
	{
		memset(szBuffRek, 0xFF, SIZEINT); /*    recordBufferLength  */
		bTempEof = 1;
		*nLR = 0;
		return bTempEof;
	}

	*nLR = job->phSrt->nLenRek;
	return bTempEof;
}
/**/
#if	defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
	INLINE  int  gcthread_GetKeys(struct job_t* job, unsigned char* szBufferIn, unsigned char* szKeyOut) {
#else
 	INLINE  int  gcthread_GetKeys(struct job_t* job, unsigned char* szBufferIn, unsigned char* szKeyOut) {
#endif
	/**/
	int nSp = 0;
	int nPos = 0;
	int nLen = 0;
	struct sortField_t* sortField;
	for (sortField = job->sortField; sortField != NULL; sortField = sortField_getNext(sortField)) {
		nPos = sortField->position;   /*  sortField_getPosition(sortField);   */
		nLen = sortField->length;     /*  sortField_getLength(sortField);     */

		gc_memcpy((unsigned char*)szKeyOut + nSp,
			(unsigned char*)szBufferIn + nPos - 1,
			nLen);
		nSp = nSp + nLen;
	}
	return 0;
}

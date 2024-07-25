/*
    ESFree V1.0 - Copyright (C) 2016 Robin Kase
    All rights reserved

    This file is part of ESFree.

    ESFree is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public licence (version 2) as published by the
    Free Software Foundation AND MODIFIED BY one exception.

    ***************************************************************************
    >>!   NOTE: The modification to the GPL is included to allow you to     !<<
    >>!   distribute a combined work that includes ESFree without being     !<<
    >>!   obliged to provide the source code for proprietary components     !<<
    >>!   outside of ESFree.                                                !<<
    ***************************************************************************

    ESFree is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. Full license text can be found on license.txt.
*/

#include "scheduler.h"

// uint8_t schedSchedulingPolicy = schedSCHEDULING_POLICY_RMS;
#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
	#include "list.h"
#endif /* schedSCHEDULING_POLICY_EDF */

#define schedTHREAD_LOCAL_STORAGE_POINTER_INDEX 0


#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
	#define schedUSE_TCB_SORTED_LIST 1
	#if( schedEDF_EFFICIENT == 1 )
		#define schedPRIORITY_RUNNING ( schedSCHEDULER_PRIORITY - 1 )
		#define schedPRIORITY_NOT_RUNNING tskIDLE_PRIORITY
	#endif /* schedEDF_EFFICIENT */
#else
	#define schedUSE_TCB_ARRAY 1
#endif /* schedSCHEDULING_POLICY_EDF */

static uint32_t contextSwitchCount = 0;
static uint32_t idleTicks = 0;
static uint32_t totalTicks = 0;


/* Extended Task control block for managing periodic tasks within this library. */
typedef struct xExtended_TCB
{
	TaskFunction_t pvTaskCode; 		/* Function pointer to the code that will be run periodically. */
	const char *pcName; 			/* Name of the task. */
	UBaseType_t uxStackDepth; 			/* Stack size of the task. */
	void *pvParameters; 			/* Parameters to the task function. */
	UBaseType_t uxPriority; 		/* Priority of the task. */
	TaskHandle_t *pxTaskHandle;		/* Task handle for the task. */
	TickType_t xReleaseTime;		/* Release time of the task. */
	TickType_t xRelativeDeadline;	/* Relative deadline of the task. */
	TickType_t xAbsoluteDeadline;	/* Absolute deadline of the task. */
	TickType_t xPeriod;				/* Task period. */
	TickType_t xLastWakeTime; 		/* Last time stamp when the task was running. */
	TickType_t xMaxExecTime;		/* Worst-case execution time of the task. */
	TickType_t xExecTime;			/* Current execution time of the task. */

	BaseType_t xWorkIsDone; 		/* pdFALSE if the job is not finished, pdTRUE if the job is finished. */

	TickType_t responseTime;			/* Response time of the task. */
    TickType_t startTime;				/* Start time of the task. */
    uint32_t deadlineMisses;		/* Number of deadline misses. */
    uint32_t maxResponseTime;		/* Maximum response time of the task. */

	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xPriorityIsSet; 	/* pdTRUE if the priority is assigned. */
		BaseType_t xInUse; 			/* pdFALSE if this extended TCB is empty. */
	#elif( schedUSE_TCB_SORTED_LIST == 1 )
		ListItem_t xTCBListItem; 	/* Used to reference TCB from the TCB list. */
		#if( schedEDF_EFFICIENT == 1 )
			ListItem_t xTCBAllListItem; /* Extra list item for xTCBAllList. */
		#endif /* schedEDF_EFFICIENT */
	#endif /* schedUSE_TCB_SORTED_LIST */

	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		BaseType_t xExecutedOnce;	/* pdTRUE if the task has executed once. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 || schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		TickType_t xAbsoluteUnblockTime; /* The task will be unblocked at this time if it is blocked by the scheduler task. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME || schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		BaseType_t xSuspended; 		/* pdTRUE if the task is suspended. */
		BaseType_t xMaxExecTimeExceeded; /* pdTRUE when execTime exceeds maxExecTime. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

} SchedTCB_t;


// static void taskStart(SchedTCB_t *task);
// static void taskComplete(SchedTCB_t *task);


#if( schedUSE_TCB_ARRAY == 1 )
	static BaseType_t prvGetTCBIndexFromHandle( TaskHandle_t xTaskHandle );
	static void prvInitTCBArray( void );
	/* Find index for an empty entry in xTCBArray. Return -1 if there is no empty entry. */
	static BaseType_t prvFindEmptyElementIndexTCB( void );
	/* Remove a pointer to extended TCB from xTCBArray. */
	static void prvDeleteTCBFromArray( BaseType_t xIndex );
#elif( schedUSE_TCB_SORTED_LIST == 1 )
	static void prvAddTCBToList( SchedTCB_t *pxTCB );
	static void prvDeleteTCBFromList(  SchedTCB_t *pxTCB );
#endif /* schedUSE_TCB_ARRAY */

static TickType_t xSystemStartTime = 0;

static void prvPeriodicTaskCode( void *pvParameters );
static void prvCreateAllTasks( void );


#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
	static void prvSetFixedPriorities( void );
#elif( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
	static void prvInitEDF( void );
	#if( schedEDF_NAIVE == 1 )
		static void prvUpdatePrioritiesEDF( void );
	#elif( schedEDF_EFFICIENT == 1 )
		static void prvInsertTCBToReadyList( SchedTCB_t *pxTCB );
	#endif /* schedEDF_NAIVE */
	#if( schedUSE_TCB_SORTED_LIST == 1 )
		static void prvSwapList( List_t **ppxList1, List_t **ppxList2 );
	#endif /* schedUSE_TCB_SORTED_LIST */
#endif /* schedSCHEDULING_POLICY_EDF */

#if( schedUSE_SCHEDULER_TASK == 1 )
	static void prvSchedulerCheckTimingError( TickType_t xTickCount, SchedTCB_t *pxTCB );
	static void prvSchedulerFunction( void );
	static void prvCreateSchedulerTask( void );
	static void prvWakeScheduler( void );
    
	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		static void prvPeriodicTaskRecreate( SchedTCB_t *pxTCB );
		static void prvDeadlineMissedHook( SchedTCB_t *pxTCB, TickType_t xTickCount );
		static void prvCheckDeadline( SchedTCB_t *pxTCB, TickType_t xTickCount );
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		static void prvExecTimeExceedHook( TickType_t xTickCount, SchedTCB_t *pxCurrentTask );
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	
#endif /* schedUSE_SCHEDULER_TASK */


#if( schedUSE_TCB_ARRAY == 1 )
	/* Array for extended TCBs. */
	static SchedTCB_t xTCBArray[ schedMAX_NUMBER_OF_PERIODIC_TASKS ] = { 0 };
	/* Counter for number of periodic tasks. */
	static BaseType_t xTaskCounter = 0;
#elif( schedUSE_TCB_SORTED_LIST == 1 )
	#if( schedEDF_NAIVE == 1 )
		static List_t xTCBList;				/* Sorted linked list for all periodic tasks. */
		static List_t xTCBTempList;			/* A temporary list used for switching lists. */
		static List_t xTCBOverflowedList; 	/* Sorted linked list for periodic tasks that have overflowed deadline. */
		static List_t *pxTCBList = NULL;  			/* Pointer to xTCBList. */
		static List_t *pxTCBTempList = NULL;		/* Pointer to xTCBTempList. */
		static List_t *pxTCBOverflowedList = NULL;	/* Pointer to xTCBOverflowedList. */
	#elif( schedEDF_EFFICIENT == 1 )
		static List_t xTCBListAll;				/* Sorted linked list for all periodic tasks. */
		static List_t xTCBBlockedList;			/* Linked list for blocked periodic tasks. */
		static List_t xTCBReadyList;			/* Sorted linked list for all ready periodic tasks. */
		static List_t xTCBOverflowedReadyList;	/* Sorted linked list for all ready periodic tasks with overflowed deadline. */
		static List_t *pxTCBListAll;					/* Pointer to xTCBListAll. */
		static List_t *pxTCBBlockedList = NULL; 		/* Pointer to xTCBBlockedList. */
		static List_t *pxTCBReadyList = NULL;			/* Pointer to xTCBReadyList. */
		static List_t *pxTCBOverflowedReadyList = NULL;	/* Pointer to xTCBOverflowedReadyList. */
		
		static SchedTCB_t *pxCurrentTCB = NULL;		/* Pointer to current executing periodic task. */
		static SchedTCB_t *pxPreviousTCB = NULL;	/* Pointer to previous executing periodic task. */
		static BaseType_t xSwitchToSchedulerOnSuspend = pdFALSE;	/* pdTRUE if context switch to scheduler task occur since a task is suspended. */
		static BaseType_t xSwitchToSchedulerOnBlock = pdFALSE;		/* pdTRUE if context switch to scheduler task occur since a task is blocked. */
		static BaseType_t xSwitchToSchedulerOnReady = pdFALSE;		/* pdTRUE if context switch to scheduler task occur since a task becomes ready. */
	#endif /* schedEDF_NAIVE */
#endif /* schedUSE_TCB_ARRAY */

#if( schedUSE_SCHEDULER_TASK )
	static TickType_t xSchedulerWakeCounter = 0;
	static TaskHandle_t xSchedulerHandle = NULL;
#endif /* schedUSE_SCHEDULER_TASK */


#if( schedUSE_TCB_ARRAY == 1 )
	/* Returns index position in xTCBArray of TCB with same task handle as parameter. */
	static BaseType_t prvGetTCBIndexFromHandle( TaskHandle_t xTaskHandle )
	{
	static BaseType_t xIndex = 0;
	BaseType_t xIterator;

		for( xIterator = 0; xIterator < schedMAX_NUMBER_OF_PERIODIC_TASKS; xIterator++ )
		{
		
			if( pdTRUE == xTCBArray[ xIndex ].xInUse && *xTCBArray[ xIndex ].pxTaskHandle == xTaskHandle )
			{
				return xIndex;
			}
		
			xIndex++;
			if( schedMAX_NUMBER_OF_PERIODIC_TASKS == xIndex )
			{
				xIndex = 0;
			}
		}
		return -1;
	}

	/* Initializes xTCBArray. */
	static void prvInitTCBArray( void )
	{
	UBaseType_t uxIndex;
		for( uxIndex = 0; uxIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS; uxIndex++)
		{
			xTCBArray[ uxIndex ].xInUse = pdFALSE;
		}
	}

	/* Find index for an empty entry in xTCBArray. Returns -1 if there is no empty entry. */
	static BaseType_t prvFindEmptyElementIndexTCB( void )
	{
	BaseType_t xIndex;
		for( xIndex = 0; xIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS; xIndex++ )
		{
			if( pdFALSE == xTCBArray[ xIndex ].xInUse )
			{
				return xIndex;
			}
		}

		return -1;
	}

	/* Remove a pointer to extended TCB from xTCBArray. */
	static void prvDeleteTCBFromArray( BaseType_t xIndex )
	{
		configASSERT( xIndex >= 0 && xIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS );
		configASSERT( pdTRUE == xTCBArray[ xIndex ].xInUse );

		if( xTCBArray[ pdTRUE == xIndex].xInUse )
		{
			xTCBArray[ xIndex ].xInUse = pdFALSE;
			xTaskCounter--;
		}
	}
	
#elif( schedUSE_TCB_SORTED_LIST == 1 )
	/* Add an extended TCB to sorted linked list. */
	static void prvAddTCBToList( SchedTCB_t *pxTCB )
	{
		/* Initialise TCB list item. */
		vListInitialiseItem( &pxTCB->xTCBListItem );
		/* Set owner of list item to the TCB. */
		listSET_LIST_ITEM_OWNER( &pxTCB->xTCBListItem, pxTCB );
		/* List is sorted by absolute deadline value. */
		listSET_LIST_ITEM_VALUE( &pxTCB->xTCBListItem, pxTCB->xAbsoluteDeadline );
		
		#if( schedEDF_EFFICIENT == 1 )
		 	/* Initialise list item for xTCBListAll. */
			vListInitialiseItem( &pxTCB->xTCBAllListItem );
			/* Set owner of list item to the TCB. */
			listSET_LIST_ITEM_OWNER( &pxTCB->xTCBAllListItem, pxTCB );
			/* There is no need to sort the list. */
		#endif /* schedEDF_EFFICIENT */

		#if( schedEDF_NAIVE == 1 )
			/* Insert TCB into list. */
			vListInsert( pxTCBList, &pxTCB->xTCBListItem );
		#elif( schedEDF_EFFICIENT == 1 )
			/* Insert TCB into ready list. */
			vListInsert( pxTCBReadyList, &pxTCB->xTCBListItem );
			/* Insert TCB into list containing tasks in any state. */
			vListInsert( pxTCBListAll, &pxTCB->xTCBAllListItem );
		#endif /* schedEDF_EFFICIENT */
	}
	
	/* Delete an extended TCB from sorted linked list. */
	static void prvDeleteTCBFromList(  SchedTCB_t *pxTCB )
	{
		#if( schedEDF_EFFICIENT == 1 )
			uxListRemove( &pxTCB->xTCBAllListItem );
		#endif /* schedEDF_EFFICIENT */
		uxListRemove( &pxTCB->xTCBListItem );
		vPortFree( pxTCB );
	}
#endif /* schedUSE_TCB_ARRAY */

#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
	#if( schedUSE_TCB_SORTED_LIST == 1 )
		/* Swap content of two lists. */
		static void prvSwapList( List_t **ppxList1, List_t **ppxList2 )
		{
		List_t *pxTemp;
			pxTemp = *ppxList1;
			*ppxList1 = *ppxList2;
			*ppxList2 = pxTemp;
		}
	#endif /* schedUSE_TCB_SORTED_LIST */

	#if( schedEDF_NAIVE == 1 )
		/* Update priorities of all periodic tasks with respect to EDF policy. */
		static void prvUpdatePrioritiesEDF( void )
		{
		SchedTCB_t *pxTCB;

			#if( schedUSE_TCB_SORTED_LIST == 1 )
				ListItem_t *pxTCBListItem;
				ListItem_t *pxTCBListItemTemp;
			
				if( listLIST_IS_EMPTY( pxTCBList ) && !listLIST_IS_EMPTY( pxTCBOverflowedList ) )
				{
					prvSwapList( &pxTCBList, &pxTCBOverflowedList );
				}

				const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
				pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );

				while( pxTCBListItem != pxTCBListEndMarker )
				{
					pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );

					/* Update priority in the SchedTCB list. */
					listSET_LIST_ITEM_VALUE( pxTCBListItem, pxTCB->xAbsoluteDeadline );

					pxTCBListItemTemp = pxTCBListItem;
					pxTCBListItem = listGET_NEXT( pxTCBListItem );
					uxListRemove( pxTCBListItem->pxPrevious );

					/* If absolute deadline overflowed, insert TCB to overflowed list. */
					if( pxTCB->xAbsoluteDeadline < pxTCB->xLastWakeTime )
					{
						vListInsert( pxTCBOverflowedList, pxTCBListItemTemp );
					}
					else /* Insert TCB into temp list in usual case. */
					{
						vListInsert( pxTCBTempList, pxTCBListItemTemp );
					}
				}

				/* Swap list with temp list. */
				prvSwapList( &pxTCBList, &pxTCBTempList );

				#if( schedUSE_SCHEDULER_TASK == 1 )
					BaseType_t xHighestPriority = schedSCHEDULER_PRIORITY - 1;
				#else
					BaseType_t xHighestPriority = configMAX_PRIORITIES - 1;
				#endif /* schedUSE_SCHEDULER_TASK */

				const ListItem_t *pxTCBListEndMarkerAfterSwap = listGET_END_MARKER( pxTCBList );
				pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );
				while( pxTCBListItem != pxTCBListEndMarkerAfterSwap )
				{
					pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );
					configASSERT( -1 <= xHighestPriority );
					pxTCB->uxPriority = xHighestPriority;
					vTaskPrioritySet( *pxTCB->pxTaskHandle, pxTCB->uxPriority );

					xHighestPriority--;
					pxTCBListItem = listGET_NEXT( pxTCBListItem );
				}
			#endif /* schedUSE_TCB_SORTED_LIST */
		}
	#elif( schedEDF_EFFICIENT == 1 )
		/* Removes given SchedTCB from blocked list and inserts it to ready list. */
		static void prvInsertTCBToReadyList( SchedTCB_t *pxTCB )
		{
			/* Remove ready task from blocked list. */
			uxListRemove( &pxTCB->xTCBListItem );

			/* Check whether absolute deadline has overflowed or not. */
			if( pxTCB->xAbsoluteDeadline < pxTCB->xLastWakeTime )
			{
				/* Absolute deadline has overflowed. */
				vListInsert( pxTCBOverflowedReadyList, &pxTCB->xTCBListItem );
			}
			else
			{
				vListInsert( pxTCBReadyList, &pxTCB->xTCBListItem );
			}

			/* If ready list is empty and overflowed ready list is not, swap those. */
			if( listLIST_IS_EMPTY( pxTCBReadyList ) && !listLIST_IS_EMPTY( pxTCBOverflowedReadyList ) )
			{
				prvSwapList( &pxTCBReadyList, &pxTCBOverflowedReadyList );
			}

			if( NULL == pxCurrentTCB || ( signed ) ( pxCurrentTCB->xAbsoluteDeadline - pxTCB->xAbsoluteDeadline ) > 0 )
			{
				if( NULL != pxCurrentTCB )
				{
					pxPreviousTCB = pxCurrentTCB;
				}
				pxCurrentTCB = pxTCB;
				xSwitchToSchedulerOnReady = pdTRUE;
				prvWakeScheduler();
			}

		}

		/* Called when a task is blocked. */
		void vSchedulerBlockTrace( void )
		{
			if( NULL != pxCurrentTCB && xTaskGetCurrentTaskHandle() == *pxCurrentTCB->pxTaskHandle )
			{
				/* Remove current task from ready list. */
				uxListRemove( &pxCurrentTCB->xTCBListItem );
				/* Insert current task to blocked list. */
				vListInsert( pxTCBBlockedList, &pxCurrentTCB->xTCBListItem );
				
				pxPreviousTCB = pxCurrentTCB;
				pxCurrentTCB = NULL;
				xSwitchToSchedulerOnBlock = pdTRUE;	
				prvWakeScheduler();
			}
		}
		
		/* Called when a task is suspended. */
		void vSchedulerSuspendTrace( TaskHandle_t xTaskHandle )
		{
		 SchedTCB_t *pxTCBToSuspend = ( SchedTCB_t * ) pvTaskGetThreadLocalStoragePointer( xTaskHandle, schedTHREAD_LOCAL_STORAGE_POINTER_INDEX );

			if( NULL != pxTCBToSuspend )
			{
				/* Remove suspended task from ready list. */
				uxListRemove( &pxTCBToSuspend->xTCBListItem );
				/* Insert suspended task to blocked list. */
				vListInsert( pxTCBBlockedList, &pxTCBToSuspend->xTCBListItem );
				
				if( *pxCurrentTCB->pxTaskHandle == *pxTCBToSuspend->pxTaskHandle )
				{
					pxPreviousTCB = pxCurrentTCB;
					pxCurrentTCB = NULL;
					xSwitchToSchedulerOnSuspend = pdTRUE;
					prvWakeScheduler();
				}
			}
		}

		/* Called when a task becomes ready. */
		void vSchedulerReadyTrace( TaskHandle_t xTaskHandle )
		{
			if( xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || xTaskHandle == xSchedulerHandle )
			{
				return;
			}

			#if( configUSE_TIMERS == 1 )
				/* Check if the unblocked task was the timer task. */
				if( xTimerGetTimerDaemonTaskHandle() == xTaskHandle )
				{
					return;
				}
			#endif /* configUSE_TIMERS */

			SchedTCB_t *pxTCBReady = ( SchedTCB_t * ) pvTaskGetThreadLocalStoragePointer( xTaskHandle, schedTHREAD_LOCAL_STORAGE_POINTER_INDEX );

			if( NULL != pxTCBReady )
			{
				prvInsertTCBToReadyList( pxTCBReady );
			}
		}
	#endif /* schedEDF_EFFICIENT */
#endif /* schedSCHEDULING_POLICY_EDF */

/* The whole function code that is executed by every periodic task.
 * This function wraps the task code specified by the user. */
static void prvPeriodicTaskCode( void *pvParameters )
{
	SchedTCB_t *pxThisTask = ( SchedTCB_t * ) pvTaskGetThreadLocalStoragePointer( xTaskGetCurrentTaskHandle(), schedTHREAD_LOCAL_STORAGE_POINTER_INDEX );
	configASSERT( NULL != pxThisTask );

	if( 0 != pxThisTask->xReleaseTime )
	{
		xTaskDelayUntil( &pxThisTask->xLastWakeTime, pxThisTask->xReleaseTime );
	}

	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		pxThisTask->xExecutedOnce = pdTRUE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	if( 0 == pxThisTask->xReleaseTime )
	{
		pxThisTask->xLastWakeTime = xSystemStartTime;
	}

	for( ; ; )
	{
		#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
			#if( schedEDF_NAIVE == 1 )
				/* Wake up the scheduler task to update priorities of all periodic tasks. */
				prvWakeScheduler();
			#endif /* schedEDF_NAIVE */
		#endif /* schedSCHEDULING_POLICY_EDF */
		pxThisTask->xWorkIsDone = pdFALSE;
        Serial.print("Task ");
        Serial.print(pxThisTask->pcName);
        Serial.print(" begin at Tickcount: ");
        Serial.println(xTaskGetTickCount());
		// taskStart(pxThisTask);
		

		/* Execute the task function specified by the user. */
		pxThisTask->pvTaskCode( pvParameters );

		pxThisTask->xWorkIsDone = pdTRUE;

		//taskComplete(pxThisTask);
        Serial.print("Task ");
        Serial.print(pxThisTask->pcName);
        Serial.print(" end at Tickcount: ");
        Serial.println(xTaskGetTickCount());

		pxThisTask->xExecTime = 0;

		#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
			pxThisTask->xAbsoluteDeadline = pxThisTask->xLastWakeTime + pxThisTask->xPeriod + pxThisTask->xRelativeDeadline;
			#if( schedEDF_EFFICIENT == 1 )
				listSET_LIST_ITEM_VALUE( &pxThisTask->xTCBListItem, pxThisTask->xAbsoluteDeadline );
			#endif /* schedEDF_EFFICIENT */

			#if( schedEDF_NAIVE == 1 )
				/* Wake up the scheduler task to update priorities of all periodic tasks. */
				prvWakeScheduler();
			#endif /* schedEDF_NAIVE */
		#endif /* schedSCHEDULING_POLICY_EDF */

		xTaskDelayUntil( &pxThisTask->xLastWakeTime, pxThisTask->xPeriod );
	}
}



/* Creates a periodic task. */
void vSchedulerPeriodicTaskCreate( TaskFunction_t pvTaskCode, const char *pcName, UBaseType_t uxStackDepth, void *pvParameters, UBaseType_t uxPriority,
		TaskHandle_t *pxCreatedTask, TickType_t xPhaseTick, TickType_t xPeriodTick, TickType_t xMaxExecTimeTick, TickType_t xDeadlineTick )
{
	taskENTER_CRITICAL();
	SchedTCB_t *pxNewTCB;
	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xIndex = prvFindEmptyElementIndexTCB();
		configASSERT( xTaskCounter < schedMAX_NUMBER_OF_PERIODIC_TASKS );
		configASSERT( xIndex != -1 );
		pxNewTCB = &xTCBArray[ xIndex ];
	#else
		pxNewTCB = pvPortMalloc( sizeof( SchedTCB_t ) );
	#endif /* schedUSE_TCB_ARRAY */


	/* Intialize item. */
	pxNewTCB->pvTaskCode = pvTaskCode;
	pxNewTCB->pcName = pcName;
	pxNewTCB->uxStackDepth = uxStackDepth;
	pxNewTCB->pvParameters = pvParameters;
	pxNewTCB->uxPriority = uxPriority;
	pxNewTCB->pxTaskHandle = pxCreatedTask;
	pxNewTCB->xReleaseTime = xPhaseTick;
	pxNewTCB->xPeriod = xPeriodTick;
	pxNewTCB->xMaxExecTime = xMaxExecTimeTick;
	pxNewTCB->xRelativeDeadline = xDeadlineTick;
	pxNewTCB->xAbsoluteDeadline = xPhaseTick + xDeadlineTick;
	pxNewTCB->xLastWakeTime = xPhaseTick;
	pxNewTCB->xExecTime = 0;
	pxNewTCB->xWorkIsDone = pdTRUE;

	pxNewTCB->responseTime = 0;
    pxNewTCB->startTime = 0;
    pxNewTCB->deadlineMisses = 0;
    pxNewTCB->maxResponseTime = 0;

	// Set priorities and deadlines based on the current scheduling policy
    switch (schedSCHEDULING_POLICY) {
        case schedSCHEDULING_POLICY_RMS:
			// pxNewTCB->xPriorityIsSet = pdTRUE;
			break;
        case schedSCHEDULING_POLICY_DM:
            // Priorities are fixed and based on the period/deadline
            pxNewTCB->uxPriority = uxPriority;
            // pxNewTCB->xPriorityIsSet = pdTRUE;
            break;
        case schedSCHEDULING_POLICY_EDF:
            // For EDF, priorities might be dynamic
            pxNewTCB->uxPriority = -1; // Managed by scheduler logic
            pxNewTCB->xAbsoluteDeadline = pxNewTCB->xRelativeDeadline + pxNewTCB->xReleaseTime + xSystemStartTime;
            // pxNewTCB->xPriorityIsSet = pdFALSE;
            break;
    }
	
	#if( schedUSE_TCB_ARRAY == 1 )
		pxNewTCB->xInUse = pdTRUE;
	#endif /* schedUSE_TCB_ARRAY */
	
	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		pxNewTCB->xExecutedOnce = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		pxNewTCB->xSuspended = pdFALSE;
		pxNewTCB->xMaxExecTimeExceeded = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	#if( schedUSE_POLLING_SERVER == 1)
		pxNewTCB->xIsPeriodicServer = pdFALSE;
	#endif /* schedUSE_POLLING_SERVER */

	#if( schedUSE_TCB_ARRAY == 1 )
		xTaskCounter++;
	#elif( schedUSE_TCB_SORTED_LIST == 1 )
		#if( schedEDF_EFFICIENT == 1 )
			pxNewTCB->uxPriority = schedPRIORITY_NOT_RUNNING;
		#endif /* schedEDF_EFFICIENT */
		prvAddTCBToList( pxNewTCB );
	#endif /* schedUSE_TCB_SORTED_LIST */

	taskEXIT_CRITICAL();
}

/* Deletes a periodic task. */
void vSchedulerPeriodicTaskDelete( TaskHandle_t xTaskHandle )
{
	if( xTaskHandle != NULL )
	{
		#if( schedUSE_TCB_ARRAY == 1 )
			prvDeleteTCBFromArray( prvGetTCBIndexFromHandle( xTaskHandle ) );
		#elif( schedUSE_TCB_SORTED_LIST == 1 )
			prvDeleteTCBFromList( ( SchedTCB_t * ) pvTaskGetThreadLocalStoragePointer( xTaskHandle, schedTHREAD_LOCAL_STORAGE_POINTER_INDEX ) );
		#endif /* schedUSE_TCB_ARRAY */
	}
	else
	{
		#if( schedUSE_TCB_ARRAY == 1 )
			prvDeleteTCBFromArray( prvGetTCBIndexFromHandle( xTaskGetCurrentTaskHandle() ) );
		#elif( schedUSE_TCB_SORTED_LIST == 1 )
			prvDeleteTCBFromList( ( SchedTCB_t * ) pvTaskGetThreadLocalStoragePointer( xTaskGetCurrentTaskHandle(), schedTHREAD_LOCAL_STORAGE_POINTER_INDEX ) );
		#endif /* schedUSE_TCB_ARRAY */
	}
	
	vTaskDelete( xTaskHandle );
}

/* Creates all periodic tasks stored in TCB array, or TCB list. */
static void prvCreateAllTasks( void )
{
SchedTCB_t *pxTCB;

	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xIndex;
		for( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
		{
			configASSERT( pdTRUE == xTCBArray[ xIndex ].xInUse );
			pxTCB = &xTCBArray[ xIndex ];

			BaseType_t xReturnValue = xTaskCreate( prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth, pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle );

			if( pdPASS == xReturnValue )
			{
				vTaskSetThreadLocalStoragePointer( *pxTCB->pxTaskHandle, schedTHREAD_LOCAL_STORAGE_POINTER_INDEX, pxTCB );
			}
			else
			{
				/* if task creation failed */
			}
		
		}
	#elif( schedUSE_TCB_SORTED_LIST == 1 )
		#if( schedEDF_NAIVE ==1 )
			const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
			ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );
		#elif( schedEDF_EFFICIENT == 1 )
			const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBListAll );
			ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBListAll );
		#endif /* schedEDF_EFFICIENT */

		while( pxTCBListItem != pxTCBListEndMarker )
		{
			pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );
			configASSERT( NULL != pxTCB );

			BaseType_t xReturnValue = xTaskCreate( prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth, pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle );
			if( pdPASS == xReturnValue )
			{

			}
			else
			{
				/* if task creation failed */
			}
			vTaskSetThreadLocalStoragePointer( *pxTCB->pxTaskHandle, schedTHREAD_LOCAL_STORAGE_POINTER_INDEX, pxTCB );
			pxTCBListItem = listGET_NEXT( pxTCBListItem );
		}	
	#endif /* schedUSE_TCB_ARRAY */
}

#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
	/* Initiazes fixed priorities of all periodic tasks with respect to RMS or
	 * DMS policy. */
static void prvSetFixedPriorities( void )
{
BaseType_t xIter, xIndex;
TickType_t xShortest, xPreviousShortest=0;
SchedTCB_t *pxShortestTaskPointer, *pxTCB;

	#if( schedUSE_SCHEDULER_TASK == 1 )
		BaseType_t xHighestPriority = schedSCHEDULER_PRIORITY;
	#else
		BaseType_t xHighestPriority = configMAX_PRIORITIES;
	#endif /* schedUSE_SCHEDULER_TASK */

	for( xIter = 0; xIter < xTaskCounter; xIter++ )
	{
		xShortest = portMAX_DELAY;

		/* search for shortest period/deadline */
		for( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
		{
			pxTCB = &xTCBArray[ xIndex ];
			configASSERT( pdTRUE == pxTCB->xInUse );
			if(pdTRUE == pxTCB->xPriorityIsSet)
			{
				continue;
			}

			#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS )
				if( pxTCB->xPeriod <= xShortest )
				{
					xShortest = pxTCB->xPeriod;
					pxShortestTaskPointer = pxTCB;
				}
			#elif( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
				if( pxTCB->xRelativeDeadline <= xShortest )
				{
					xShortest = pxTCB->xRelativeDeadline;
					pxShortestTaskPointer = pxTCB;
				}
			#endif /* schedSCHEDULING_POLICY */
		}
		configASSERT( -1 <= xHighestPriority );
		if( xPreviousShortest != xShortest )
		{
			xHighestPriority--;
		}
		/* set highest priority to task with xShortest period (the highest priority is configMAX_PRIORITIES-1) */
		pxShortestTaskPointer->uxPriority = xHighestPriority;
		pxShortestTaskPointer->xPriorityIsSet = pdTRUE;

		xPreviousShortest = xShortest;
	}
}
#elif( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
	/* Initializes priorities of all periodic tasks with respect to EDF policy. */
	static void prvInitEDF( void )
	{
	SchedTCB_t *pxTCB;

		#if( schedEDF_NAIVE == 1 )
			#if( schedUSE_SCHEDULER_TASK == 1 )
				UBaseType_t uxHighestPriority = schedSCHEDULER_PRIORITY - 1;
			#else
				UBaseType_t uxHighestPriority = configMAX_PRIORITIES - 1;
			#endif /* schedUSE_SCHEDULER_TASK */

			const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
			ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );

			while( pxTCBListItem != pxTCBListEndMarker )
			{
				pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );

				pxTCB->uxPriority = uxHighestPriority;
				uxHighestPriority--;

				pxTCBListItem = listGET_NEXT( pxTCBListItem );
			}
		#elif( schedEDF_EFFICIENT == 1 )
			ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBReadyList );
			pxCurrentTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );
			pxCurrentTCB->uxPriority = schedPRIORITY_RUNNING;
		#endif /* schedEDF_EFFICIENT */
	}
#endif /* schedSCHEDULING_POLICY */


#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )

	/* Recreates a deleted task that still has its information left in the task array (or list). */
	static void prvPeriodicTaskRecreate( SchedTCB_t *pxTCB )
	{
	BaseType_t xReturnValue = xTaskCreate( prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth, pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle );
		if( pdPASS == xReturnValue )
		{
			vTaskSetThreadLocalStoragePointer( *pxTCB->pxTaskHandle, schedTHREAD_LOCAL_STORAGE_POINTER_INDEX, ( SchedTCB_t * ) pxTCB );

			/* This must be set to false so that the task does not miss the deadline immediately when it is created. */
			pxTCB->xExecutedOnce = pdFALSE;
			#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
				pxTCB->xSuspended = pdFALSE;
				pxTCB->xMaxExecTimeExceeded = pdFALSE;
			#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

			#if( schedEDF_EFFICIENT == 1)
				prvInsertTCBToReadyList( pxTCB );
			#endif/* schedEDF_EFFICIENT */
		}
		else
		{
			/* if task creation failed */
		}
	}

	/* Called when a deadline of a periodic task is missed.
	 * Deletes the periodic task that has missed it's deadline and recreate it.
	 * The periodic task is released during next period. */
	static void prvDeadlineMissedHook( SchedTCB_t *pxTCB, TickType_t xTickCount )
	{
		//printf( "\r\ndeadline missed! %s tick %d\r\n", pxTCB->pcName, xTickCount );
        pxTCB->deadlineMisses++;
		/* Delete the pxTask and recreate it. */
		vTaskDelete( *pxTCB->pxTaskHandle );
		pxTCB->xExecTime = 0;
		prvPeriodicTaskRecreate( pxTCB );

		pxTCB->xReleaseTime = pxTCB->xLastWakeTime + pxTCB->xPeriod;
		/* Need to reset lastWakeTime for correct release. */
		pxTCB->xLastWakeTime = 0;
		pxTCB->xAbsoluteDeadline = pxTCB->xRelativeDeadline + pxTCB->xReleaseTime;
		#if( schedEDF_EFFICIENT == 1 )
			listSET_LIST_ITEM_VALUE( &pxTCB->xTCBListItem, pxTCB->xAbsoluteDeadline );
		#endif /* schedEDF_EFFICIENT */
	}

	/* Checks whether given task has missed deadline or not. */
	static void prvCheckDeadline( SchedTCB_t *pxTCB, TickType_t xTickCount )
	{
		if( ( NULL != pxTCB ) && ( pdFALSE == pxTCB->xWorkIsDone ) && ( pdTRUE == pxTCB->xExecutedOnce ) )
		{
			/* Need to update absolute deadline if the scheduling policy is not EDF. */
			#if( schedSCHEDULING_POLICY != schedSCHEDULING_POLICY_EDF )
				pxTCB->xAbsoluteDeadline = pxTCB->xLastWakeTime + pxTCB->xRelativeDeadline;
			#endif /* schedSCHEDULING_POLICY */

			/* Using ICTOH method proposed by Carlini and Buttazzo, to check whether deadline is missed. */
			if( ( signed ) ( pxTCB->xAbsoluteDeadline - xTickCount ) < 0 )
			{
				/* Deadline is missed. */
				prvDeadlineMissedHook( pxTCB, xTickCount );
			}
		}
	}
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */


#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )

	/* Called if a periodic task has exceeded it's worst-case execution time.
	 * The periodic task is blocked until next period. A context switch to
	 * the scheduler task occur to block the periodic task. */
	static void prvExecTimeExceedHook( TickType_t xTickCount, SchedTCB_t *pxCurrentTask )
	{
		//Serial.print( "\r\nworst case execution time exceeded! %s %d %d\r\n", pxCurrentTask->pcName, pxCurrentTask->xExecTime, xTickCount );
        pxCurrentTask->maxResponseTime++;
		pxCurrentTask->xMaxExecTimeExceeded = pdTRUE;
		/* Is not suspended yet, but will be suspended by the scheduler later. */
		pxCurrentTask->xSuspended = pdTRUE;
		pxCurrentTask->xAbsoluteUnblockTime = pxCurrentTask->xLastWakeTime + pxCurrentTask->xPeriod;
		pxCurrentTask->xExecTime = 0;
		#if( schedUSE_POLLING_SERVER == 1)
			if( pdTRUE == pxCurrentTask->xIsPeriodicServer )
			{
				pxCurrentTask->xAbsoluteDeadline = pxCurrentTask->xAbsoluteUnblockTime + pxCurrentTask->xRelativeDeadline;
				#if( schedEDF_EFFICIENT == 1 )
					listSET_LIST_ITEM_VALUE( &pxCurrentTask->xTCBListItem, pxCurrentTask->xAbsoluteDeadline );
				#endif /* schedEDF_EFFICIENT */
			}
		#endif /* schedUSE_POLLING_SERVER */
		BaseType_t xHigherPriorityTaskWoken;
		vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
		//portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
	}

#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */


#if( schedUSE_SCHEDULER_TASK == 1 )
	/* Called by the scheduler task. Checks all tasks for any enabled
	 * Timing Error Detection feature. */
	static void prvSchedulerCheckTimingError( TickType_t xTickCount, SchedTCB_t *pxTCB )
	{
		#if( schedUSE_TCB_ARRAY == 1 )
			if( pdFALSE == pxTCB->xInUse )
			{
				return;
			}
		#endif

		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
			#if( schedUSE_POLLING_SERVER == 1 )
				/* If the task is periodic server, do not check deadline. */
				if( pdTRUE == pxTCB->xIsPeriodicServer )
				{
					#if( schedUSE_SPORADIC_JOBS == 1 )
						prvCheckSporadicJobDeadline(  xTickCount );
					#endif /* schedUSE_SPORADIC_JOBS */
				}
				else
				{
					/* Since lastWakeTime is updated to next wake time when the task is delayed, tickCount > lastWakeTime implies that
					 * the task has not finished it's job this period. */

					/* Using ICTOH method proposed by Carlini and Buttazzo, to check the condition unaffected by counter overflows. */
					if( ( signed ) ( xTickCount - pxTCB->xLastWakeTime ) > 0 )
					{
						pxTCB->xWorkIsDone = pdFALSE;
					}

					prvCheckDeadline( pxTCB, xTickCount );
				}
			#else
				/* Since lastWakeTime is updated to next wake time when the task is delayed, tickCount > lastWakeTime implies that
				 * the task has not finished it's job this period. */

				/* Using ICTOH method proposed by Carlini and Buttazzo, to check the condition unaffected by counter overflows. */
				if( ( signed ) ( xTickCount - pxTCB->xLastWakeTime ) > 0 )
				{
					pxTCB->xWorkIsDone = pdFALSE;
				}

				prvCheckDeadline( pxTCB, xTickCount );
			#endif /* schedUSE_POLLING_SERVER */
		#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
		

		#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
			if( pdTRUE == pxTCB->xMaxExecTimeExceeded )
			{
				pxTCB->xMaxExecTimeExceeded = pdFALSE;
				vTaskSuspend( *pxTCB->pxTaskHandle );
			}
			if( pdTRUE == pxTCB->xSuspended )
			{
				/* Using ICTOH method proposed by Carlini and Buttazzo, to check whether absolute unblock time is reached. */
				if( ( signed ) ( pxTCB->xAbsoluteUnblockTime - xTickCount ) <= 0 )
				{
					pxTCB->xSuspended = pdFALSE;
					pxTCB->xLastWakeTime = xTickCount;
					vTaskResume( *pxTCB->pxTaskHandle );
				}
			}
		#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

		return;
	}

	/* Function code for the scheduler task. */
	static void prvSchedulerFunction( void )
	{
		for( ; ; )
		{
			#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
				#if( schedEDF_NAIVE == 1 )
					prvUpdatePrioritiesEDF();
				#elif( schedEDF_EFFICIENT == 1 )
					if( pdTRUE == xSwitchToSchedulerOnBlock )
					{
						if( NULL != pxPreviousTCB )
						{
							vTaskPrioritySet( *pxPreviousTCB->pxTaskHandle, schedPRIORITY_NOT_RUNNING );
							pxPreviousTCB->uxPriority = schedPRIORITY_NOT_RUNNING;
						}
						if( !listLIST_IS_EMPTY( pxTCBReadyList ) )
						{
							ListItem_t *pxListItem = listGET_HEAD_ENTRY( pxTCBReadyList );
							pxCurrentTCB = listGET_LIST_ITEM_OWNER( pxListItem );
							if( NULL != *pxCurrentTCB->pxTaskHandle )
							{
								vTaskPrioritySet( *pxCurrentTCB->pxTaskHandle, schedPRIORITY_RUNNING );
								pxCurrentTCB->uxPriority = schedPRIORITY_RUNNING;
							}
						}
						
						xSwitchToSchedulerOnBlock = pdFALSE;
					}
					else if( pdTRUE == xSwitchToSchedulerOnSuspend )
					{
						if( NULL != *pxPreviousTCB->pxTaskHandle )
						{
							vTaskPrioritySet( *pxPreviousTCB->pxTaskHandle, schedPRIORITY_NOT_RUNNING );
							pxPreviousTCB->uxPriority = schedPRIORITY_NOT_RUNNING;
						}
						if( !listLIST_IS_EMPTY( pxTCBReadyList ) )
						{
							ListItem_t *pxListItem = listGET_HEAD_ENTRY( pxTCBReadyList );
							pxCurrentTCB = listGET_LIST_ITEM_OWNER( pxListItem );

							if( NULL != *pxCurrentTCB->pxTaskHandle )
							{
								vTaskPrioritySet( *pxCurrentTCB->pxTaskHandle, schedPRIORITY_RUNNING );
								pxCurrentTCB->uxPriority = schedPRIORITY_RUNNING;
							}
						}
						
						xSwitchToSchedulerOnSuspend = pdFALSE;
					}
					else if( pdTRUE == xSwitchToSchedulerOnReady )
					{
						if( NULL != pxPreviousTCB )
						{
							vTaskPrioritySet( *pxPreviousTCB->pxTaskHandle, schedPRIORITY_NOT_RUNNING );
							pxPreviousTCB->uxPriority = schedPRIORITY_NOT_RUNNING;
						}
						if( NULL != pxCurrentTCB )
						{
							vTaskPrioritySet( *pxCurrentTCB->pxTaskHandle, schedPRIORITY_RUNNING );
							pxCurrentTCB->uxPriority = schedPRIORITY_RUNNING;
						}
						
						xSwitchToSchedulerOnReady = pdFALSE;
					}					
				#endif /* schedEDF_EFFICIENT */
			#endif /* schedSCHEDULING_POLICY_EDF */

			#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
				TickType_t xTickCount = xTaskGetTickCount();
				SchedTCB_t *pxTCB;

				#if( schedUSE_TCB_ARRAY == 1 )
					BaseType_t xIndex;
					for( xIndex = 0; xIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS; xIndex++ )
					{
						pxTCB = &xTCBArray[ xIndex ];
						prvSchedulerCheckTimingError( xTickCount, pxTCB );
					}
				#elif( schedUSE_TCB_SORTED_LIST == 1 )
					#if( schedEDF_NAIVE == 1 )
						const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
						ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );
					#elif( schedEDF_EFFICIENT == 1)
						const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBListAll );
						ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBListAll );
					#endif /* schedEDF_EFFICIENT */
					while( pxTCBListItem != pxTCBListEndMarker )
					{
						pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem);

						prvSchedulerCheckTimingError( xTickCount, pxTCB );

						pxTCBListItem = listGET_NEXT( pxTCBListItem );
					}
				#endif /* schedUSE_TCB_SORTED_LIST */
			
			#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

			ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
		}
	}

	/* Creates the scheduler task. */
	static void prvCreateSchedulerTask( void )
	{
		xTaskCreate( (TaskFunction_t) prvSchedulerFunction, "Scheduler", schedSCHEDULER_TASK_STACK_SIZE, NULL, schedSCHEDULER_PRIORITY, &xSchedulerHandle );
	}
#endif /* schedUSE_SCHEDULER_TASK */


#if( schedUSE_SCHEDULER_TASK == 1 )
	/* Wakes up (context switches to) the scheduler task. */
	static void prvWakeScheduler( void )
	{
	BaseType_t xHigherPriorityTaskWoken;
		vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
		//portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
	}

	/* Called every software tick. */
	void vApplicationTickHook( void )
	{

		totalTicks++;
		// Serial.print("totalTicks: ");
		// Serial.println( totalTicks);
		SchedTCB_t *pxCurrentTask;
		TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();

		pxCurrentTask = ( SchedTCB_t * ) pvTaskGetThreadLocalStoragePointer( xCurrentTaskHandle, schedTHREAD_LOCAL_STORAGE_POINTER_INDEX );
		if( NULL != pxCurrentTask && xCurrentTaskHandle != xSchedulerHandle && xCurrentTaskHandle != xTaskGetIdleTaskHandle() )
		{
			pxCurrentTask->xExecTime++;
			#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
				if( pxCurrentTask->xMaxExecTime <= pxCurrentTask->xExecTime )
				{
					if( pdFALSE == pxCurrentTask->xMaxExecTimeExceeded )

					{
						if( pdFALSE == pxCurrentTask->xSuspended )
						{
							prvExecTimeExceedHook( xTaskGetTickCountFromISR(), pxCurrentTask );
						}
					}
				}
			#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
		}

		// #if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
			xSchedulerWakeCounter++;
			if( xSchedulerWakeCounter == schedSCHEDULER_TASK_PERIOD )
			{
				xSchedulerWakeCounter = 0;
				prvWakeScheduler();
			}
		// #endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	}
#endif /* schedUSE_SCHEDULER_TASK */



void externTaskSwitchedIn() {
    contextSwitchCount++;
    SchedTCB_t* currentTask = (SchedTCB_t*) pvTaskGetThreadLocalStoragePointer(xTaskGetCurrentTaskHandle(), schedTHREAD_LOCAL_STORAGE_POINTER_INDEX);
    if (currentTask) {
        currentTask->startTime = xTaskGetTickCount();  // Capture start time
    }
}

void externTaskSwitchedOut() {
    SchedTCB_t* currentTask = (SchedTCB_t*) pvTaskGetThreadLocalStoragePointer(xTaskGetCurrentTaskHandle(), schedTHREAD_LOCAL_STORAGE_POINTER_INDEX);
    if (currentTask) {
        TickType_t now = xTaskGetTickCount();
        TickType_t responseTime = now - currentTask->startTime;
        currentTask->responseTime += responseTime;  // Accumulate response time
        if (responseTime > currentTask->maxResponseTime) {
            currentTask->maxResponseTime = responseTime;  // Update max response time
        }
        if (now > currentTask->xAbsoluteDeadline) {
            currentTask->deadlineMisses++;  // Count deadline misses
        }
    }
}
void reportIdleTick() {
    idleTicks++;
}

// static void taskStart(SchedTCB_t *task) {
//     task->startTime = xTaskGetTickCount();
// }

// static void taskComplete(SchedTCB_t *task) {
//     uint32_t now = xTaskGetTickCount();
//     uint32_t responseTime = now - task->startTime;
//     task->responseTime += responseTime;
//     Serial.print("Task "); Serial.print(task->pcName); Serial.println(" Completed.");
//     Serial.print("   Current Response Time: "); Serial.println(responseTime);
//     Serial.print("   Accumulated Response Time: "); Serial.println(task->responseTime);

//     if (now > task->xAbsoluteDeadline) {
//         task->deadlineMisses++;
//         Serial.print("   Deadline Missed! Total Misses: "); Serial.println(task->deadlineMisses);
//     }

//     if (responseTime > task->maxResponseTime) {
//         task->maxResponseTime = responseTime;
//         Serial.print("   New Worst Case Response Time: "); Serial.println(task->maxResponseTime);
//     }
// }

void initializePerformanceMetrics() {
	#if( schedUSE_TCB_ARRAY == 1 )
		memset(xTCBArray, 0, sizeof(xTCBArray));
	#elif( schedUSE_TCB_SORTED_LIST == 1 )
		//
	#endif
    Serial.println("Performance Metrics Initialized");
}

void printMetrics() {
    Serial.println("Performance Metrics:");
    //Serial.print("Total Context Switches: "); Serial.println(contextSwitchCount);
    //Serial.print("CPU Load: "); Serial.print(100.0 * (totalTicks - idleTicks) / totalTicks); Serial.println("%");

    #if (schedUSE_TCB_ARRAY == 1)
        for (int i = 0; i < schedMAX_NUMBER_OF_PERIODIC_TASKS; i++) {
            if (xTCBArray[i].xInUse) {
                Serial.print("Task "); Serial.print(xTCBArray[i].pcName); Serial.println(" Metrics:");
               // Serial.print("   Total Response Time: "); Serial.println(xTCBArray[i].responseTime);
                Serial.print("   Deadline Misses: "); Serial.println(xTCBArray[i].deadlineMisses);
                Serial.print("   Worst Case Response Time Exceeded: "); Serial.println(xTCBArray[i].maxResponseTime);
            }
        }
    #elif (schedUSE_TCB_SORTED_LIST == 1)
		#if( schedEDF_NAIVE == 1 )
			List_t *pxList= &xTCBList;
		#else
			List_t *pxList= &xTCBListAll;
			
		#endif

        // List_t *pxList = (schedEDF_NAIVE == 1) ? &xTCBList : &xTCBListAll;
        const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER(pxList);
        ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY(pxList);

        int i = 0;
        while (pxTCBListItem != pxTCBListEndMarker) {
            SchedTCB_t *pxTCB = listGET_LIST_ITEM_OWNER(pxTCBListItem);
            Serial.print("Task "); Serial.print(pxTCB->pcName); Serial.println(" Metrics:");
            //Serial.print("   Total Response Time: "); Serial.println(pxTCB->responseTime);
            Serial.print("   Deadline Misses: "); Serial.println(pxTCB->deadlineMisses);
            Serial.print("   Worst Case Response Time Exceeded: "); Serial.println(pxTCB->maxResponseTime);
            i++;
            pxTCBListItem = listGET_NEXT(pxTCBListItem);
        }
    #endif
}
/* This function must be called before any other function call from this module. */
void vSchedulerInit( void )
{
	#if( schedUSE_TCB_ARRAY == 1 )
		prvInitTCBArray();
	#elif( schedUSE_TCB_SORTED_LIST == 1 )
		#if( schedEDF_NAIVE == 1 )
			vListInitialise( &xTCBList );
			vListInitialise( &xTCBTempList );
			vListInitialise( &xTCBOverflowedList );
			pxTCBList = &xTCBList;
			pxTCBTempList = &xTCBTempList;
			pxTCBOverflowedList = &xTCBOverflowedList;
		#elif( schedEDF_EFFICIENT == 1 )
			vListInitialise( &xTCBListAll );
			vListInitialise( &xTCBBlockedList );
			vListInitialise( &xTCBReadyList );
			vListInitialise( &xTCBOverflowedReadyList );
			pxTCBListAll = &xTCBListAll;
			pxTCBBlockedList = &xTCBBlockedList;
			pxTCBReadyList = &xTCBReadyList;
			pxTCBOverflowedReadyList = &xTCBOverflowedReadyList;
		#endif /* schedEDF_NAIVE */
	#endif /* schedUSE_TCB_ARRAY */
}

/* Starts scheduling tasks. All periodic tasks (including polling server) must
 * have been created with API function before calling this function. */
void vSchedulerStart( void )
{  Serial.println("Scheduler started.");
	totalTicks = 0;

	#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS )
		Serial.println("inside RMS.");
		prvSetFixedPriorities();
	#elif( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
		Serial.println("inside EDF.");

		prvInitEDF();

	#endif /* schedSCHEDULING_POLICY */

	#if( schedUSE_SCHEDULER_TASK == 1 )
		prvCreateSchedulerTask();
	#endif /* schedUSE_SCHEDULER_TASK */

	prvCreateAllTasks();

	xSystemStartTime = xTaskGetTickCount();
	vTaskStartScheduler();
	  Serial.println("Scheduler started.");

}

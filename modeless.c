/* Copyright (c) 2017, Computer History Museum All rights reserved. Redistribution and use in source and binary forms, with or without modification, are permitted (subject to the limitations in the disclaimer below) provided that the following conditions are met:  * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.  * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following    disclaimer in the documentation and/or other materials provided with the distribution.  * Neither the name of Computer History Museum nor the names of its contributors may be used to endorse or promote products    derived from this software without specific prior written permission. NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */#include "modeless.h"#define FILE_NUM 26/* Copyright (c) 1990-1992 by the University of Illinois Board of Trustees */#pragma segment UtilStackHandle ModalStack;extern short AlertDefault(short template);MyWindowPtr GetNewMyAlert(short template,UPtr wStorage,MyWindowPtr winStorage,WindowPtr behind,StageList *stages);void HandleModalEvent(EventRecord *theEvent,DialogPtr theDialog);void HandleMouseDown (EventRecord	*theEvent,DialogPtr theDialog);void HandleMenu (long mSelect,DialogPtr theDialog);void MySendBehind (WindowPtr theWindow, WindowPtr behindWindow);enum {    uppMovableModalProcInfo = kPascalStackBased         | RESULT_SIZE(SIZE_CODE(sizeof(Boolean)))         | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(DialogPtr)))         | STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(EventRecord)))         | STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(short)))};#define STOP_ICON_DITL 20100#define CAUTION_ICON_DITL 20200#define NOTE_ICON_DITL 20300/************************************************************************ * GetNewMyDialog - get a new dialog, with a bit extra ************************************************************************/// (jp) We've grown a new parameter, win, to support memory preflighting for//			a world of opaque data structures.  We formerly passed in a pointer to an//			extended DialogRecord in 'wStorage'... which we won't be able to do under//			Carbon.  In fact, under Carbon we can't preflight memory at all -- so this//			will have to eventually change again.  For the time being, we're passing in//			pre-allocated storage for both the DialogRecord and our own window structure.MyWindowPtr GetNewMyDialog(short template,UPtr wStorage,MyWindowPtr win,WindowPtr behind){	DialogPtr		theDialog;		if (win == nil)	{		if (HandyMyWindow)		{			win = HandyMyWindow;			HandyMyWindow = nil;		}		else if ((win=New(MyWindow))==nil)			return (nil);	}	WriteZero(win, sizeof(MyWindow));		theDialog = GetNewDialog(template, (void*)wStorage, behind);	if (theDialog==nil) { ZapPtr (win); return(nil); }		SetDialogMyWindowPtr (theDialog, win);	win->theWindow = GetDialogWindow(theDialog);		win->isDialog = win->isRunt = True;	win->dialogID = template;	SetPort(GetDialogPort(theDialog));	GetDialogPortBounds(theDialog,&win->contR);	SetMyWindowPrivateData (win, CREATOR);	GetRColor(&win->textColor,TEXT_COLOR);	GetRColor(&win->backColor,BACK_COLOR);	win->titleBarHi = win->uselessHi = win->leftRimWi = win->uselessWi = -1;	win->windex = ++Windex;	MySetThemeWindowBackground(win, kThemeActiveModelessDialogBackgroundBrush, false);	win->backBrush = kThemeActiveModelessDialogBackgroundBrush;	return(win);}/************************************************************************ * DoModelessEvent - handle (the result of) an event in a modeless dialog ************************************************************************/Boolean DoModelessEvent(DialogPtr dlog,EventRecord *event){	MyWindowPtr	dlogWin = GetDialogMyWindowPtr (dlog);	long select;	Boolean result = false;	short item;	char key = event->message & charCodeMask;	DialogPtr outDlog;	Boolean mine = IsMyWindow(GetDialogWindow(dlog));		SetPort_(GetWindowPort(GetDialogWindow(dlog)));	#ifdef SETTINGSLISTKEY	if ((event->what==keyDown) || (event->what==autoKey))		if (mine && dlogWin->key)			if (result = (*(dlogWin)->key)(dlogWin,event))				goto done;#endif	/*	 * check for cmdkey equivalents	 */	if (event->what==keyDown)	{		if (event->modifiers&cmdKey)		{			if (select=MyMenuKey(event))			{				DoMenu(FrontWindow_(),select,event->modifiers);				result = 0;				goto done;			}			else if (key=='.' && mine)			{				if (dlogWin->hit)					result = (*(dlogWin)->hit)(event,dlog,2,dlogWin->dialogRefcon);				goto done;			}		}		else if (key==escChar && mine)		{			if (dlogWin->hit)				result = (*(dlogWin)->hit)(event,dlog,2,dlogWin->dialogRefcon);			goto done;		}		else if (key==delChar)		{			result = 0;			goto done;		}		else if (mine && (key==returnChar || key==enterChar) && !dlogWin->ignoreDefaultItem)		{				if (dlogWin->hit)					result = (*(dlogWin)->hit)(event,dlog,1,dlogWin->dialogRefcon);				goto done;		}		else if (key==tabChar && event->modifiers&shiftKey)		{			BackTab(dlog);			result = 0;			goto done;		}	}	/*	 * do the event	 */#ifdef SETTINGSLISTKEY	if (mine && dlogWin->filter) result = ((*(dlogWin)->filter)(dlogWin,event));		result = MyDialogSelect(event,&outDlog,&item) && outDlog==dlog && mine && dlogWin->hit ?			(*(dlogWin)->hit)(event,dlog,item,dlogWin->dialogRefcon) : False;#else	if (mine && dlogWin->filter) (*(dlogWin)->filter)(dlogWin,event);		result = MyDialogSelect(event,&outDlog,&item) && outDlog==dlog && mine && dlogWin->hit?		(*(dlogWin)->hit)(event,dlog,item,dlogWin->dialogRefcon) : False;#endif		/*	 * if drawing, outline the ok button	 */	if (event->what==updateEvt || event->what==activateEvt) HiliteButtonOne(dlog);		if (event->what==app4Evt) DoApp4(MyFrontWindow(),event);done:	EnableMenus(FrontWindow_(),False);	return(result);}/************************************************************************ * BackTab - tab backwards in a dialog ************************************************************************/void BackTab(DialogPtr dlog){	short curItem = GetDialogKeyboardFocusItem(dlog);	short item;	Rect itemR;	short type;	Handle itemH;	short selectMe = 0;		for (item=curItem-1;item>0;item--)	{		GetDialogItem(dlog,item,&type,&itemH,&itemR);		if (type==editText) {selectMe = item; break;}	}				if (!selectMe)		for (item=CountDITL(dlog);item>curItem;item--)		{			GetDialogItem(dlog,item,&type,&itemH,&itemR);			if (type==editText) {selectMe = item; break;}		}		if (selectMe) SelectDialogItemText(dlog,selectMe,0,REAL_BIG);}/************************************************************************ * DoModelessEdit - handle the Edit menu for a modeless dialog ************************************************************************/Boolean DoModelessEdit(MyWindowPtr win,short item){	DialogPtr	winDP = GetMyWindowDialogPtr (win);	short	err;	SAVE_STUFF;	SetBGGrey(0xffff);	// (jp) If the dialog is using PETE's instead of TE's, just return, because editing will	//			be handled by PETE. (We can't currently mix PETE's with edit items...)	if (IsMyWindow (GetDialogWindow(winDP)) && win->pte) {		REST_STUFF;		return (false);	}		// password dialog doesn't like edit menu	if (win->noEditMenu)	{		REST_STUFF;		SysBeep(20L);		return false;	}		switch(item)	{		case EDIT_CUT_ITEM:			DialogCut(winDP);			if ((err=ClearCurrentScrap()) || (err=TEToScrap()))				WarnUser(COPY_FAILED,err);			break;		case EDIT_COPY_ITEM:			DialogCopy(winDP);			if (err=TEToScrap())				WarnUser(COPY_FAILED,err);			break;		case EDIT_PASTE_ITEM:			if (!IsScrapFull())				SysBeep(20);			else if (err=TEFromScrap())				WarnUser(PASTE_FAILED,err);			else				DialogPaste(winDP);			break;		case EDIT_CLEAR_ITEM:			DialogDelete(winDP);			break;		case EDIT_SELECT_ITEM:			SelectDialogItemText(winDP,GetDialogKeyboardFocusItem(winDP),0,REAL_BIG);			break;		case EDIT_UNDO_ITEM:			break;		default:			REST_STUFF;			return(False);	}	REST_STUFF;	return(True);}void	StartMovableModal(DialogPtr dialog){	MyWindowPtr	dialogWin = GetDialogMyWindowPtr (dialog);	short item;	HiliteMenu(0);	PositionPrefsTitle(false,dialogWin);	PushModalWindow(GetDialogWindow(dialog));	EnableMenus(ModalWindow,false);	dialogWin->dontControl = true;	SetMyCursor(arrowCursor);	item = GetDialogDefaultItem(dialog);	if (!item) item=1;	SetDialogDefaultItem(dialog,item);	TEFromScrap();}//	(jp)	PushModalWindow must receive a WindowPtr rather than a MyWindowPtr since not all//				dialogs are created with a MyWindowPtr hanging off of the refconvoid PushModalWindow(WindowPtr theWindow){	if (!ModalStack) StackInit(sizeof(WindowPtr),&ModalStack);	if (ModalStack) StackPush(&ModalWindow,ModalStack);	ModalWindow = theWindow;}void PopModalWindow(void){	ModalWindow = nil;	if (ModalStack) StackPop(&ModalWindow,ModalStack);	if (!DirtyHackForChooseMailbox && gMenuBarIsSetup)		EnableMenus(ModalWindow,false);	//JDB 8/20/97 all should be false.}void	EndMovableModal(DialogPtr dialog){	PositionPrefsTitle(true,GetDialogMyWindowPtr(dialog));	PopModalWindow();}pascal void	MovableModalDialog(DialogPtr myDialog,ModalFilterUPP theFilterProc,short *itemHit){	MyWindowPtr	myDialogWin = GetDialogMyWindowPtr (myDialog);	EventRecord	oldMain = MainEvent;	DialogPtr		outDlog;	short				peteItemHit;	Boolean			hitPETEUserPane;	#ifndef DRAG_GETOSEVT	ASSERT(!Dragging);	if (Dragging)	{		CommandPeriod = True;		*itemHit = CANCEL_ITEM;		return;	}#endif	PushCursor(arrowCursor);	for (;;)	{		WNE(everyEvent & ~highLevelEventMask,&MainEvent,0);		if (MainEvent.modifiers&cmdKey)			HiliteMenu(0);		if (MyIsDialogEvent(&MainEvent))			{				hitPETEUserPane = AllWeAreSayingIsGivePeteAChance (myDialogWin, &MainEvent, &peteItemHit);				if (theFilterProc != nil && InvokeModalFilterUPP(myDialog,&MainEvent,itemHit,theFilterProc))					break;				else					if (MyDialogSelect(&MainEvent, &outDlog, itemHit) && outDlog==myDialog)						break;				if (hitPETEUserPane) {					*itemHit = peteItemHit;					break;				}			}		else			HandleEvent(&MainEvent);			//HandleModalEvent(&theEvent,myDialog);	}		PopCursor();	MainEvent = oldMain;}/************************************************************************ * GetNewMyAlert - get a new dialog, with a bit extra ************************************************************************/// (jp) We've grown a new parameter, winStorage, to support memory preflighting for//			a world of opaque data structures.  We formerly passed in a pointer to an//			extended DialogRecord in 'wStorage'... which we won't be able to do under//			Carbon.  In fact, under Carbon we can't preflight memory at all -- so this//			will have to eventually change again.  For the time being, we're passing in//			pre-allocated storage for both the DialogRecord and our own window structure.MyWindowPtr GetNewMyAlert(short template,UPtr wStorage,MyWindowPtr win,WindowPtr behind,StageList *stages){	DialogPtr	theDialog;	AlertTemplate theTemplate,**theAlertHandle;	Handle	itemListHandle;	Rect 		screenRect;		if (win == nil)	{		if (HandyMyWindow)		{			win = HandyMyWindow;			HandyMyWindow = nil;		}		else if ((win=New(MyWindow))==nil)			return (nil);	}	WriteZero(win, sizeof(MyWindow));	GetQDGlobalsScreenBitsBounds(&screenRect);	theAlertHandle = (void **)GetResource('ALRT',template);	theTemplate = **theAlertHandle;	itemListHandle = GetResource('DITL',theTemplate.itemsID);	MyHandToHand(&itemListHandle);			*stages = theTemplate.stages;	ThirdCenterRectIn(&theTemplate.boundsRect,&screenRect);	theDialog = ThereIsColor?		NewColorDialog((void*)wStorage,&theTemplate.boundsRect,"\p",false,5,behind,false,CREATOR,itemListHandle)		: NewDialog((void*)wStorage,&theTemplate.boundsRect,"\p",false,5,behind,false,CREATOR,itemListHandle);	if (theDialog==nil) { ZapPtr (win); return(nil); }		SetDialogMyWindowPtr (theDialog, win);	win->theWindow = GetDialogWindow(theDialog);		win->isDialog = win->isRunt = True;	SetPort(GetDialogPort(theDialog));	GetDialogPortBounds(theDialog,&win->contR);	SetMyWindowPrivateData (win, CREATOR);	MySetThemeWindowBackground(win, kThemeActiveModelessDialogBackgroundBrush, false);	win->backBrush = kThemeActiveModelessDialogBackgroundBrush;	return(win);}pascal short MovableAlert(short alertID,short which,ModalFilterUPP theFilterProc){	MyWindowPtr	theAlertWin = nil;	DialogPtr	theAlert = nil;	StageList theStageList;	short	item = REAL_BIG;	short	numOfItems;	Handle	itemListHandle;	MenuHandle	tempMenuHandle = nil;	uLong		oldForceSend;		PushGWorld();		theAlertWin = GetNewMyAlert(alertID,nil,nil,InFront,&theStageList);		theAlert = GetMyWindowDialogPtr(theAlertWin);	if (!theAlert)	{		PopGWorld();		return (-1);	}	numOfItems = CountDITL(theAlert);	switch (which)	{		case Normal:			break;		case Stop:			itemListHandle = GetResource('DITL',STOP_ICON_DITL);			break;		case Note:				itemListHandle = GetResource('DITL',NOTE_ICON_DITL);			break;		case Caution:			itemListHandle = GetResource('DITL',CAUTION_ICON_DITL);			break;		default:			which = Normal;			break;	}		if (which != Normal)	{		if (!MyHandToHand(&itemListHandle))			AppendDITL(theAlert,itemListHandle,overlayDITL);	}		SetDialogDefaultItem(theAlert,1);	theAlertWin->dontControl = true;	ShowWindow(GetDialogWindow(theAlert));	SetPort(GetDialogPort(theAlert));	HiliteMenu(0);		PushModalWindow(GetDialogWindow(theAlert));	if (gMenuBarIsSetup)	{		tempMenuHandle = GetMHandle(FILE_MENU);		if (tempMenuHandle)			DisableItem(tempMenuHandle,0);		tempMenuHandle = GetMHandle(APPLE_MENU);		if (tempMenuHandle)			DisableItem(tempMenuHandle,1);		tempMenuHandle = GetMHandle(MAILBOX_MENU);		if (tempMenuHandle)			DisableItem(tempMenuHandle,0);		tempMenuHandle = GetMHandle(EDIT_MENU);		if (tempMenuHandle)			DisableItem(tempMenuHandle,0);		tempMenuHandle = GetMHandle(MESSAGE_MENU);		if (tempMenuHandle)			DisableItem(tempMenuHandle,0);		tempMenuHandle = GetMHandle(TRANSFER_MENU);		if (tempMenuHandle)			DisableItem(tempMenuHandle,0);		tempMenuHandle = GetMHandle(SPECIAL_MENU);		if (tempMenuHandle)			DisableItem(tempMenuHandle,0);		tempMenuHandle = GetMHandle(WINDOW_MENU);		if (tempMenuHandle)			DisableItem(tempMenuHandle,0);				DrawMenuBar();	}	oldForceSend = ForceSend;	ForceSend = 1;	do	{		MovableModalDialog(theAlert,theFilterProc,&item);	} while (item >= numOfItems);	ForceSend = oldForceSend;	DisposDialog_(theAlert);	PopModalWindow();	CommandPeriod = false;	PopGWorld();	return (item);}void HandleModalEvent(EventRecord *theEvent,DialogPtr theDialog){		if (theEvent->what == mouseDown)					HandleMouseDown(theEvent,theDialog);}/**** * HandleMouseDown (theEvent) * *	Take care of mouseDown events. * ****/void HandleMouseDown (EventRecord	*theEvent,DialogPtr theDialog){	WindowPtr	theWindow;	int			windowCode = FindWindow (theEvent->where, &theWindow);	Rect		screenBits;    switch (windowCode)      {	  case inMenuBar:	    HandleMenu(MenuSelect(theEvent->where),theDialog);	    break;	  	  case kHighLevelEvent:	  	break;	  case inDrag:	  		if (theWindow==theDialog)	  			DragWindow(theWindow, theEvent->where,GetQDGlobalsScreenBitsBounds(&screenBits));	  		else	  			SysBeep(20);	  	  break;	  	  	  case inContent:	  		if (theWindow!=GetDialogWindow(theDialog))	  			SysBeep(20);	  	break;	  		  case inGoAway:	  	  break;      }}/* end HandleMouseDown *//***** * * HandleMenu(mSelect) * *	Handle the menu selection. mSelect is what MenuSelect() and *	MenuKey() return: the high word is the menu ID, the low word *	is the menu item * *****/enum {	appleID = 128,	fileID,	editID	};enum {	kCutItem=3,	kCopyItem,	kPasteItem,	};void HandleMenu (long mSelect,DialogPtr theDialog){	int			menuID = HiWord(mSelect);	int			menuItem = LoWord(mSelect);		switch (menuID)	  {	  case	editID:		switch (menuItem)			{				case kCutItem:					DialogCut(theDialog);					break;				case kCopyItem:					DialogCopy(theDialog);					break;				case kPasteItem:					DialogPaste(theDialog);					break;			}		break;			  }	  HiliteMenu(0);}/* end HandleMenu */////	MyIsDialogEvent//Boolean MyIsDialogEvent (const EventRecord *theEvent){	GrafPtr			oldPort;	Boolean			result;		GetPort (&oldPort);	result = IsDialogEvent (theEvent);	SetPort (oldPort);	return (result);}////	MyDialogSelect////		Hack to move dialogs in front of floaters (temporarily) before calling DialogSelect//Boolean MyDialogSelect(const EventRecord *theEvent, DialogPtr *theDialog, short *itemHit){	GrafPtr			oldPort;	Boolean			result;		GetPort (&oldPort);	result = DialogSelect (theEvent, theDialog, itemHit);	SetPort (oldPort);	return (result);}Boolean IsModelessDialog (WindowPtr theWindow){	return (IsMyWindow (theWindow) && GetWindowKind(theWindow) == dialogKind && GetWindowGoAwayFlag (theWindow));}////	MySendBehind////		Like SendBehind, but just fiddles with the windowlist -- without generating update and activate events//void MySendBehind (WindowPtr targetWindow, WindowPtr behindWindow){	SendBehind(targetWindow,behindWindow);}//	Previously, all of the storage we allocated when creating a dialog was disposed by the Dialog Manager,//	simply by disposing of thedialog itself.  Since we can no longer use extended dialog records we have//	to hang our own window structure off of the dialog's refcon field, so we have to manage disposal of//	this data ourselvesvoid MyDisposeDialog (DialogPtr dlog){	MyWindowPtr	dlogWin;		if (IsMyWindow (GetDialogWindow(dlog))) {		dlogWin = GetDialogMyWindowPtr (dlog);		ZapPtr (dlogWin);		SetDialogMyWindowPtr (dlog, dlogWin);	}	DisposeDialog (dlog);}/************************************************************************ * AreAllModalsPlugwindows - are all modal windows plugin windows? ************************************************************************/Boolean AreAllModalsPlugwindows(){	Boolean		isModalPlugwindow = IsPlugwindow(ModalWindow);		if (isModalPlugwindow && ModalStack && ((*ModalStack)->elCount > 1))	{		WindowPtr	theModalWindow;		long		elCount = (*ModalStack)->elCount;		short		i;				for (i = 1; i < elCount; i++)		{			StackItem(&theModalWindow,i,ModalStack);			if (!IsPlugwindow(theModalWindow))			{				isModalPlugwindow = false;				break;			}		}	}		return isModalPlugwindow;}
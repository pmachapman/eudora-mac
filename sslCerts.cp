/* Copyright (c) 2017, Computer History Museum All rights reserved. Redistribution and use in source and binary forms, with or without modification, are permitted (subject to the limitations in the disclaimer below) provided that the following conditions are met:  * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.  * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following    disclaimer in the documentation and/or other materials provided with the distribution.  * Neither the name of Computer History Museum nor the names of its contributors may be used to endorse or promote products    derived from this software without specific prior written permission. NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. *//*	File: sslCerts.cp	Purpose:	Routines for getting certificates for SSL operations*/#define DEBUGASSERT 0#define DEBUGERR 0#define DEBUGMESSAGE 0#define DEBUGDATA 0#ifndef _SSLCERTS_H_#include "sslCerts.h"#endif#include "EZCrypto.h"#if TARGET_API_MAC_CARBON#include <KeychainCore.h>#include <KeychainHI.h>#include "MachOWrapper.h"#endif#include "md5.h"#pragma mark -- OS 9 routines ---extern Boolean gKeychainChanged;enum { 	kKCGetRootCertificateKeychain	= 14 };EXTERN_API( OSStatus )KCDispatch				(UInt16				commandID,						 void *				value);static OSStatus OS9ReadCertsFromKC ( SSLContext *sslContext, AddCertProc addCerts, KCRef kc ) {	OSStatus err;	KCAttributeList attrList;	KCAttribute attr;	KCItemAttr itemClass;	KCSearchRef search;	KCItemRef item;	Handle certData;	long certBuffSize = 1;	SSLBuffer berCert;//	Allocate a buffer to hold the certs//	Why NuHTempBetter doesn't return a handle is beyond me!	if ( !( certData = (Handle) NuHTempBetter ( certBuffSize )))		return memFullErr;		itemClass = kCertificateKCItemClass;	attrList.count = 1;	attrList.attr = &attr;	attr.tag = kClassKCItemAttr;	attr.length = sizeof(itemClass);	attr.data = &itemClass;	/* Get the first item */	if (( err = KCFindFirstItem ( kc, &attrList, &search, &item )) == noErr ) {		Boolean lastItem = false;				/* The first time, it will be 1 */		berCert.length = certBuffSize;		do {	// for each certificate			do {	// until the buffer is big enough			/*	Is the buffer too small? */				if ( certBuffSize < berCert.length ) {				/*	Make it bigger */					certBuffSize = berCert.length;					SetHandleSize ( certData, certBuffSize );					if (( err = MemError ()) != noErr )						break;					}			/*	Get the cert data */				HLock ( certData );				err = KCGetData ( item, certBuffSize, *certData, &berCert.length );				HUnlock ( certData );			} while(err == errKCBufferTooSmall);					/*	Release the current cert item; we've still got the data in the handle */			KCReleaseItem ( &item );		//	To get out of a doubly-nested loop, you have to break twice!			if ( err != noErr )				break;					/*	Add the cert to the SSL context */			HLock ( certData );			berCert.data = (unsigned char *) *certData;			err = addCerts ( sslContext, berCert, 1 );			HUnlock ( certData );			} while (err >= noErr && ( err = KCFindNextItem ( search, &item )) == noErr );				KCReleaseSearch(&search);		}		DisposeHandle ( certData );	return (err == errKCItemNotFound) ? noErr : err;	}static OSStatus OS9ReadCerts ( SSLContext *sslContext, AddCertProc addCerts ) {	KCRef kc;	OSStatus err = noErr;	gKeychainChanged = false;	#if (!TARGET_API_MAC_CARBON)//	Make sure we've got the call to get the root cert keychain	if ((long) KCDispatch == kUnresolvedCFragSymbolAddress )		return errKCWrongKCVersion;	//	Open the root cert keychain	if (( err = KCDispatch ( kKCGetRootCertificateKeychain, &kc )) == noErr ) {	//	Add all the certs in the root certificate keychain		err = OS9ReadCertsFromKC ( sslContext, addCerts, kc );		KCReleaseKeychain ( &kc );		}#endif		if ( err == noErr ) {	//	Add all the certs in the default keychain, too		if (( err = KCGetDefaultKeychain ( &kc )) == noErr ) {			err = OS9ReadCertsFromKC ( sslContext, addCerts, kc );			KCReleaseKeychain(&kc);			}		}			return err;	}static Boolean	NagHit ( EventRecord *event, DialogPtr dlog, short item, long refcon ) {	AliasHandle alias;	FSSpec kcSpec, spec;	Boolean wasChanged;		CloseMyWindow ( GetDialogWindow ( dlog ));	if( !FSResolveFID ( Root.vRef, refcon, &spec )) {		if ( item != kStdOkItemIndex )			FSpDelete ( &spec );		else {	// launch the keychain with the file we just saved			if ( noErr == CreatorToApp ( 'kcmr', &alias )) {				if ( noErr == ResolveAlias ( nil, alias, &kcSpec, &wasChanged ))					OpenDocWith ( &spec, &kcSpec, false );				if ( alias != NULL )					DisposeHandle ((Handle) alias );				}			}		}	return true;	}static OSStatus OS9AddCert ( SSLCertificateChain *certs ) {	OSStatus err = noErr;	FSSpec certSpec;	//	Create a temp file, write the contents of the cert to it and let the user know about it	if ( !NewTempSpec ( Root.vRef, 0, nil, &certSpec )) {		long fid;				err = FSpCreate ( &certSpec, CREATOR, 'cert', smSystemScript );		if ( err == noErr ) {			short certRefNum;			err = AFSpOpenDF ( &certSpec, &certSpec, fsRdWrPerm, &certRefNum );			if ( err == noErr ) {				long count = certs->berCert.length;				AWrite ( certRefNum, &count, certs->berCert.data );				MyFSClose ( certRefNum );				FSMakeFID ( &certSpec, &fid );				Nag ( SSL_CERT_DLOG_9, nil, NagHit, nil, false, fid );				}			}		}	return err;	}#pragma mark -- OS X routines --#define	kCDSACertClass	(0x80000000 + 0x1000)typedef struct OpaqueSecCertificateRef	*SecCertificateRef;typedef struct OpaqueSecTrustRef		*SecTrustRef;typedef struct OpaquePolicySearchRef	*SecPolicySearchRef;typedef struct OpaqueSecPolicyRef		*SecPolicyRef;typedef uint32 CSSM_CERT_TYPE;typedef uint32 CSSM_CERT_ENCODING;#define	CSSM_CERT_X_509v3		0x03#define	CSSM_CERT_ENCODING_DER	0x03static OSStatus OSXReadCerts ( SSLContext *sslContext, AddCertProc addCerts ) {	OSStatus 				err = noErr;	SecKeychainSearchRef	search;	SecKeychainItemRef 		item;		MachOWrapper<OSStatus (*)(CFTypeRef, KCItemClass, const SecKeychainAttributeList *, SecKeychainSearchRef *)>		SecKeychainSearchCreateFromAttributes ( CFSTR ( "Security.framework" ), CFSTR ( "SecKeychainSearchCreateFromAttributes" ));	MachOWrapper<OSStatus (*)(SecKeychainSearchRef, SecKeychainItemRef *)>		SecKeychainSearchCopyNext ( CFSTR ( "Security.framework" ), CFSTR ( "SecKeychainSearchCopyNext" ));//	I've kind of butchered the prototype here; the second param to SecCertificateGetData is supposed to//	be a CSSM_DATA record, but that has the same structure as an SSLBuffer, and that's what I need here.	MachOWrapper<OSStatus (*)(SecKeychainItemRef, SSLBuffer *)>		SecCertificateGetData ( CFSTR ( "Security.framework" ), CFSTR ( "SecCertificateGetData" ));		err = SecKeychainSearchCreateFromAttributes ( NULL, kCDSACertClass, NULL, &search );	if ( err == noErr ) {		do {			item = NULL;						err = SecKeychainSearchCopyNext ( search, &item );		//	On a brand new keychain in 10.3, this routine returns a whacked out error code		//	(0x80011109 to be precise) instead of 'errKCItemNotFound' - we ignore this.			if ( err != noErr )				err = errKCItemNotFound;			if ( err == noErr ) {				SSLBuffer berCert;			//	Get the data from the certificate item				err = SecCertificateGetData ( item, &berCert );				if ( err == noErr )					err = addCerts ( sslContext, berCert, 1 );									CFRelease ( item );				}			} while ( err == noErr );			CFRelease ( search );		}		return err == errKCItemNotFound ? noErr : err;	}static OSStatus AppendChar ( Handle h, char c ) {	OSStatus err = noErr;	long oldSize = GetHandleSize ( h );	SetHandleSize ( h, oldSize + 1 );	if ( noErr == ( err = MemError ())) {		(*h) [ oldSize ] = c;		}	return err;	}static OSStatus AppendToHandle ( Handle h, char *text, long sz ) {	OSStatus err = noErr;	long oldSize = GetHandleSize ( h );	SetHandleSize ( h, oldSize + sz );	if ( noErr == ( err = MemError ()))		BlockMoveData ( text, *h + oldSize, sz );	return err;	}static OSStatus AppendToHandle ( Handle h, StringPtr text ) {	OSStatus err = noErr;	long oldSize = GetHandleSize ( h );	SetHandleSize ( h, oldSize + text [ 0 ] );	if ( noErr == ( err = MemError ()))		BlockMoveData ( text + 1, *h + oldSize, text [ 0 ] );	return err;	}static OSStatus AppendToHandle ( Handle h, UInt32 strID ) {	OSStatus err = noErr;	Str255	aString;		GetRString ( aString, strID );	if ( aString [ 0 ] > 0 ) 		err = AppendToHandle ( h, aString );	return err;	}static OSStatus AppendDateToHandle ( Handle h, UInt32 date ) {	OSStatus err = noErr;//	convert the date to a string	char *t = ctime ((time_t *) &date );//	cTime appends a '\n' onto the end of the string (which we don't want)	err = AppendToHandle ( h, t, strlen ( t ) - 1 );	return err;	}static unsigned char NibbleToChar ( int value ) { return "\p0123456789ABCDEF" [ ( value & 0x0F ) + 1 ]; }static OSStatus AppendHex ( Handle h, unsigned char *data, UInt32 len ) {	OSStatus err = noErr;	//	convert the buffer to hex	Ptr p = NewPtr ( len * 3 );	if ( p == NULL )		return MemError ();	Ptr q = p;	for ( int i = 0; i < len; ++i ) {		*q++ = ' ';		*q++ = NibbleToChar ( data [ i ] >> 4 );		*q++ = NibbleToChar ( data [ i ] );		}		err = AppendToHandle ( h, p, q - p );	DisposePtr ( p );	return err;	}static OSStatus AppendToHandle ( Handle h, const SSLBuffer &buf ) {	return AppendHex ( h, buf.data, buf.length );	}static OSStatus AppendToHandle ( Handle h, const SSLAVA &ava ) {	OSStatus err = noErr;		if ( ava.avaData.data != NULL ) {		if ( ava.tag == TagPrintableString || ava.tag == TagIA5String )			err = AppendToHandle ( h, (char *) ava.avaData.data, ava.avaData.length );	//	else // do something else!!		}		return err;	}static Boolean FindDN ( SSLContext *sslContext, void *certData, unsigned long oid, SSLAVA &ava ) {//	Yeah, this is compute intensive, but WTF	unsigned long count;	SSLErr sslErr = SSLCountSubjectDNFields ( certData, &count );	if ( sslErr == SSLNoErr ) {		for ( unsigned long i = 0; sslErr == SSLNoErr && i < count; ++i ) {			if ( SSLNoErr != SSLExtractSubjectDNFieldIndex ( certData, i, &ava, sslContext ))				return false;			if ( ava.oid == oid )				return true;			}		}	return false;	}static OSErr AppendHash ( Handle h, const SSLCertificateChain *certs, EZCryptoType hashAlg ) {	unsigned char digest [ 30 ];	// long enough	UInt32 len;	EZCryptoObj ez;		EZCreateObject ( &ez );	EZInitHash   ( ez, hashAlg );	EZUpdateHash ( ez, certs->berCert.data, certs->berCert.length );	EZFinalHash  ( ez, digest, sizeof ( digest ), &len );	EZDestroyObject ( &ez );		return AppendHex ( h, digest, len );		}static Handle GetCertText ( SSLContext *sslContext, const SSLCertificateChain *certs ) {	Handle retVal = NewHandle ( 0 );	SSLBuffer buf;	UInt32	startDate, endDate;	SSLAVA ava;	//	HostName:	AppendToHandle ( retVal, SSL_CERTDLG_HOSTNAMAE );			SSLExtractSubjectDNField ( certs->certData, OID_commonName, &ava, sslContext );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	//	Expiration date:	AppendToHandle ( retVal, SSL_CERTDLG_EXPIRES );	SSLExtractValidityDates ( certs->certData, &startDate, &endDate );	AppendDateToHandle ( retVal, endDate );	AppendChar ( retVal, '\r' );//	Fingerprints:	AppendToHandle ( retVal, SSL_CERTDLG_FINGERPRINTS );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_SHA1 );	AppendHash ( retVal, certs, ezSHA1 );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_MD5 );	AppendHash ( retVal, certs, ezMD5 );	AppendChar ( retVal, '\r' );//	What kind of cert is it?//	AppendToHandle ( retVal, SSL_CERTDLG_TYPE );	AppendChar ( retVal, '\r' );//	Serial #	AppendToHandle ( retVal, SSL_CERTDLG_SERIAL );	SSLExtractSerialNumber ( certs->certData, &buf, sslContext );	AppendToHandle ( retVal, buf );	SSLFreeSerialNumber ( &buf, sslContext );	AppendChar ( retVal, '\r' );		AppendToHandle ( retVal, SSL_CERTDLG_BEFORE );	AppendDateToHandle ( retVal, startDate );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_AFTER );	AppendDateToHandle ( retVal, endDate );	AppendChar ( retVal, '\r' );	AppendChar ( retVal, '\r' );	//	Who's that girl...	AppendToHandle ( retVal, SSL_CERTDLG_ISSUER );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_COUNTRY );	FindDN ( sslContext, certs->certData, OID_countryName, ava );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_STATE );	FindDN ( sslContext, certs->certData, OID_stateProvinceName, ava );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_LOCALITY );	FindDN ( sslContext, certs->certData, OID_localityName, ava );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_ORGANIZATION );	FindDN ( sslContext, certs->certData, OID_organizationName, ava );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_ORGUNIT );	FindDN ( sslContext, certs->certData, OID_organizationalUnitName, ava );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_CNAME );	FindDN ( sslContext, certs->certData, OID_commonName, ava );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_EMAIL );	FindDN ( sslContext, certs->certData, OID_emailAddress, ava );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendChar ( retVal, '\r' );//	Running around with you...	AppendToHandle ( retVal, SSL_CERTDLG_SUBJECT );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_COUNTRY );	SSLExtractSubjectDNField ( certs->certData, OID_countryName, &ava, sslContext );	AppendToHandle ( retVal, ava );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_STATE );	SSLExtractSubjectDNField ( certs->certData, OID_stateProvinceName, &ava, sslContext );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_LOCALITY );	SSLExtractSubjectDNField ( certs->certData, OID_localityName, &ava, sslContext );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_ORGANIZATION );	SSLExtractSubjectDNField ( certs->certData, OID_organizationName, &ava, sslContext );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_ORGUNIT );	SSLExtractSubjectDNField ( certs->certData, OID_organizationalUnitName, &ava, sslContext );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_CNAME );	SSLExtractSubjectDNField ( certs->certData, OID_commonName, &ava, sslContext );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendToHandle ( retVal, SSL_CERTDLG_EMAIL );	SSLExtractSubjectDNField ( certs->certData, OID_emailAddress, &ava, sslContext );	AppendToHandle ( retVal, ava );	SSLFreeAVA ( &ava, sslContext );	AppendChar ( retVal, '\r' );	AppendChar ( retVal, '\r' );	return retVal;	}static Handle GetDialogItemHandle ( DialogPtr theDialog, short itemNo ) {	DialogItemType	itemType;	Handle			item;	Rect			box;	GetDialogItem ( theDialog, itemNo, &itemType, &item, &box );	return item;	}//	All we want to do is to prevent people from typing chars into the edit-text itemstatic pascal Boolean XShowFilterProc ( DialogRef theDialog, EventRecord *theEvent, DialogItemIndex *itemHit ) {//	All non-key events are handled by the standard filter	if ( theEvent->what != keyDown && theEvent->what != autoKey )		return StdFilterProc ( theDialog, theEvent, itemHit );//	Command keys are fine, too	if ( theEvent->modifiers & cmdKey )		return StdFilterProc ( theDialog, theEvent, itemHit );//	OK, cancel	char key = (char) ( theEvent->message & charCodeMask );	switch ( key ) {		case kHomeCharCode:		case kEndCharCode:		case kPageUpCharCode:		case kPageDownCharCode:		case kLeftArrowCharCode:		case kRightArrowCharCode:		case kUpArrowCharCode:		case kDownArrowCharCode:			return StdFilterProc ( theDialog, theEvent, itemHit );	//	For some reason, if I try to let the standard filter proc	//	handle these three keys, then they go into the TextEdit field,	//	instead of doing to the dialog		case kReturnCharCode:		case kEnterCharCode:			*itemHit = kStdOkItemIndex;			return true;					case kEscapeCharCode:			*itemHit = kStdCancelItemIndex;			return true;					default:			*itemHit = -1;			SysBeep ( 1 );			return true;		}//	Shouldn't ever get here	return false;		}	static OSStatus XShowCertToUser ( SSLContext *sslContext, const SSLCertificateChain *certs ) {	OSStatus err = noErr;//	Show the alert with the text		MyWindowPtr theWin = GetNewMyDialog ( SSL_CERT_DLOG_X, NULL, NULL, (WindowPtr) InFront );	if ( theWin == NULL )		return memFullErr;	DialogPtr theDialog = GetMyWindowDialogPtr ( theWin );//	Set the dialog prompt	Str255	aString;		GetRString ( aString, SSL_CERT_PROMPT );	SetDialogItemText ( GetDialogItemHandle ( theDialog, 4 ), aString );//	Set the certificate information	Handle	certText = GetCertText ( sslContext, certs );	TESetText ( *certText, GetHandleSize ( certText ), GetDialogTextEditHandle ( theDialog ));	DisposeHandle ( certText );	//	Set the ok and cancel buttons	SetDialogDefaultItem ( theDialog, kStdOkItemIndex );	SetDialogCancelItem  ( theDialog, kStdCancelItemIndex );//	Display the dialog	ModalFilterUPP	filterProc = NewModalFilterUPP ( XShowFilterProc );	short dItem;	Boolean done = false;	StartMovableModal (theDialog );	ShowWindow ( GetDialogWindow ( theDialog ));	HiliteButtonOne ( theDialog );	while ( !done ) {		MovableModalDialog ( theDialog, filterProc, &dItem );		switch ( dItem ) {			case kStdOkItemIndex:			case kStdCancelItemIndex:				done = true;				break;			}		}		EndMovableModal ( theDialog );	MyDisposeDialog ( theDialog );	DisposeModalFilterUPP ( filterProc );		return dItem == kStdOkItemIndex ? noErr : userCanceledErr ;	}//	How many certs are there in the default keychain?static int CountKCCerts ( SecKeychainRef theKC ) {	int		retVal = 0;	OSErr	err = noErr;		MachOWrapper<OSStatus (*)( CFTypeRef keychainOrArray, unsigned long itemClass, const SecKeychainAttributeList *attrList, SecKeychainSearchRef *searchRef )>		SecKeychainSearchCreateFromAttributes ( CFSTR ( "Security.framework" ), CFSTR ( "SecKeychainSearchCreateFromAttributes" ));	SecKeychainSearchRef searchRef;	if ( noErr == ( err = SecKeychainSearchCreateFromAttributes ( theKC, kCDSACertClass, NULL, &searchRef ))) {		MachOWrapper<OSStatus (*)( SecKeychainSearchRef searchRef, SecKeychainItemRef *itemRef )>			SecKeychainSearchCopyNext ( CFSTR ( "Security.framework" ), CFSTR ( "SecKeychainSearchCopyNext" ));				SecKeychainItemRef itemRef;		while ( noErr == ( err = SecKeychainSearchCopyNext ( searchRef, &itemRef ))) {			retVal++;			CFRelease ( itemRef );			}				CFRelease ( searchRef );		}			return retVal;	}static OSStatus OSXAddCert ( SSLContext *sslContext, const SSLCertificateChain *certs ) {	OSStatus err = noErr;	SecCertificateRef certRef;	//	I've kind of butchered the prototype here; the first param to SecCertificateCreateFromData is supposed to//	be a CSSM_DATA record, but that has the same structure as an SSLBuffer, and that's what I need here.	MachOWrapper<OSStatus (*)(const SSLBuffer *, CSSM_CERT_TYPE, CSSM_CERT_ENCODING, SecCertificateRef *)>		SecCertificateCreateFromData ( CFSTR ( "Security.framework" ), CFSTR ( "SecCertificateCreateFromData" ));//	Create the cert from the cert data	err = SecCertificateCreateFromData ( &certs->berCert, CSSM_CERT_X_509v3, CSSM_CERT_ENCODING_DER, &certRef );	if ( err == noErr ) {	//	Ask the user if he wants to approve this cert		if ( noErr == ( err = XShowCertToUser ( sslContext, certs ))) {			MachOWrapper<OSStatus (*)( SecKeychainRef *)>				SecKeychainCopyDefault ( CFSTR ( "Security.framework" ), CFSTR ( "SecKeychainCopyDefault" ));				SecKeychainRef defKC;			if ( noErr == ( err = SecKeychainCopyDefault ( &defKC ))) {				int oldCount, newCount;								oldCount = CountKCCerts ( defKC );				MachOWrapper<OSStatus (*)(SecCertificateRef, SecKeychainRef )>					SecCertificateAddToKeychain ( CFSTR ( "Security.framework" ), CFSTR ( "SecCertificateAddToKeychain" ));				err = SecCertificateAddToKeychain ( certRef, NULL );				newCount = CountKCCerts ( defKC );				ASSERT ( err == noErr && newCount - oldCount == 1 );				CFRelease ( defKC );				}			}					CFRelease ( certRef );		}		return err;	}	#pragma mark -- routines defined in the header --static Boolean HaveJaguar () {	return GetOSVersion () >= 0x1020;	}Boolean	CanDoSSL () {	return CanReadCertificates ();	}//	General interrogation - can we get certs from somewhere?Boolean CanReadCertificates () {//	We're OK on MacOS 9	if ( !HaveOSX ()) return true;//	If we've got Jaguar, we're OK	if ( HaveJaguar ()) return true;	//	Otherwise, we're screwed	return false;	}//	Get the certificates from somewhereOSStatus ReadCertsFromStore ( SSLContext *sslContext, AddCertProc addCerts ) {	if ( !CanReadCertificates ())		return unimpErr;		OSStatus err;	if ( HaveOSX ())		err = OSXReadCerts ( sslContext, addCerts );	else		err = OS9ReadCerts ( sslContext, addCerts );		return err;	}//	Add a cert to the cert storeOSStatus AddCertToStore ( SSLContext *sslContext, SSLCertificateChain *certs ) {	if ( !CanReadCertificates ())		return unimpErr;#ifndef	VRELEASE//	Save the cert to a temp file for later examination	FSSpec certFile;		short refNum;		(void) FSMakeFSSpec ( Root.vRef, Root.dirId, "\pmtcTempCert", &certFile );//	Create the file - we don't care if it already exists	(void) FSpCreate ( &certFile, 'cs0m', 'DER ', smRoman );//	Open the file and write the cert to it	if ( noErr == FSpOpenDF ( &certFile, fsRdWrPerm, &refNum )) {		long len = certs->berCert.length;		(void) SetEOF ( refNum, len );		(void) FSWrite ( refNum, &len, certs->berCert.data );		FSClose ( refNum );		}#endif			OSStatus err;	if ( HaveOSX ())		err = OSXAddCert ( sslContext, certs );	else		err = OS9AddCert ( certs );			return err;	}
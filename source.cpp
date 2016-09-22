/* --------------------------

		MML 解析プログラム 

	------------------------ */
// Use WinmmLibraryFile(ソース内に書き込むやり方
#pragma comment (lib,"winmm.lib")
#include "MMLStruct.h"
#include "Resource.h"
#include <mmsystem.h>	// メディアライブラリ
#include <process.h>	// 別スレッド使用

// 定数
#define MMLPLAYAPI extern "C" __declspec( dllexport ) 
#define CHs 6
#define Interval 10
#define Fault2(x){ MessageBox( NULL , x , "MMLPlay fault msg" , 0 ); return 0; }
#define Fault(x) { MessageBox( NULL , x , "MMLPlay fault msg" , 0 ); free( lpBuf );_endthreadex(0); }
MMLPLAYAPI void Configure( );

HMIDIOUT hMidi;				/* midiのハンドル */
LONG iTimerID;				/* MMTimerのID */
HANDLE hThread;				/* ThreadHandle */
class INFO Info[ CHs+1 ];
HINSTANCE hDLLInst;

/* --------- 初期化ルーチン ------------*/
BOOL MMLPlay_Init( ){
	HKEY RegEntry;DWORD MIDIIndex = -1 , MIDISize = sizeof(LONG);
	// プロトタイプ宣言
	void CALLBACK mmTimer( UINT , UINT , DWORD , DWORD , DWORD );
	VOID MMLPlay_Quit( );
	
		/* -- レジストリから設定リード -- */
		Reader:
		RegOpenKeyEx( HKEY_CURRENT_USER , "Software¥¥CReN" , 0 , KEY_READ , &RegEntry );
		RegQueryValueEx( RegEntry , "" , NULL , NULL , (LPBYTE)&MIDIIndex, &MIDISize );
	
		RegCloseKey( RegEntry );
		if ( MIDIIndex == - 1 ) {
			Configure( );
			goto Reader;
		}
	/* -- ミディをオープン -- */
	if ( !hMidi ) midiOutOpen( &hMidi , MIDIIndex , 0 , 0 , 0 );
	if ( !hMidi )
		Fault2( "MIDIドライバがオープンできませんでした。" );
	/* -- タイマーセット -- */
	if ( iTimerID == NULL ){
		iTimerID = timeSetEvent( Interval , 1 , mmTimer , 0 , 1 );
		if ( iTimerID == NULL ){
			MMLPlay_Quit( );
			Fault2( "タイマーが起動できません" );
		}
	}
	return 1;
}
/* --------- 開放ルーチン -------------*/
VOID MMLPlay_Quit( ){
	/* -- タイマーを破棄 -- */
	if ( iTimerID != NULL ){
		timeKillEvent( iTimerID ); iTimerID = NULL;
	}
	/* -- ミディバイバイ！ -- */
	if ( hMidi ) {
		midiOutReset(hMidi);
		midiOutClose(hMidi);
		hMidi = NULL;
	}
	for ( int i = 0 ; i < CHs ; i++ )
		Info[ i ].BufFree( );
}

INT_PTR CALLBACK DialogProc( HWND , UINT , WPARAM , LPARAM  );

void MMLPlay( LPSTR lpAug ){
	// プロトタイプ宣言
	unsigned __stdcall MMLPlayMain( void* );
	// 変数定義
	unsigned threadID;		/* ThreadID */

	/* --- MMLがNULLなら解放のみ行う --- */
	if ( lpAug[ 0 ] == '¥0' ){
		MMLPlay_Quit( ); return ;
	}

	INT_PTR CALLBACK DialogProc( HWND , UINT , WPARAM , LPARAM  );

	hThread = (HANDLE)_beginthreadex( NULL, 0, &MMLPlayMain, lpAug, 0, &threadID );

}
/* -- ダイアログの表示 -- */
MMLPLAYAPI void Configure( ){
	DialogBox(
		hDLLInst ,MAKEINTRESOURCE( IDD_CONFIG ) , 
		NULL , (DLGPROC)DialogProc );
}
bool MMLPlay_State( ) {
	for ( int i = 0 ; i < CHs ; i++ )
		if ( Info[ i ] .Buffers )return true;
	return false;
}
/* ---------連続する数字を読み込む --------- */
BYTE GetNum( LPSTR *MMLSource , BYTE *lpVal ){
	BYTE Data = 0;

	while ( isdigit( *(*MMLSource) ) ){
		Data = Data * 10 + ( *( *(MMLSource ) ) - '0' );
		(*MMLSource)++;
	}
	*lpVal = Data;
	return Data;
}
/* --------- MML実現ルーチン --------- */
#define Range(x,y,z) ((y<=x) && (x<=z))

unsigned __stdcall MMLPlayMain( void* pArguments ) {
	LPSTR MMLString , lpBuf;	/* 内部保存用バッファ */

	/* -- バッファ確保 -- */
	MMLString = lpBuf = (LPSTR)malloc( lstrlen( ( LPSTR )pArguments ) + 1 );
	lstrcpy( lpBuf , ( LPSTR )pArguments );
	while ( *MMLString != '¥0' ){
		*MMLString = _tolower( *MMLString ); MMLString++;
	} MMLString = lpBuf;

	// バッファビルトアップ
	for ( int i = 0 ; i < CHs ; i++ ) 
		Info[ i ].BufMalloc( );

	/* -- 変数定義 -- */
	long Ret = 0 ;		/* 戻り値 */
	BYTE CH = 0;

	/* -- 解析開始 -- */
	while ( *MMLString != '¥0' ){
		if ( iscntrl( *MMLString ) || ( *MMLString == 32 ) ) { MMLString++;continue;}
		/* -- コマンド文字の取得 -- */
		switch ( *( MMLString++ ) ){
		case '<':
			/* オクターブアップ */
			if ( ( ++Info[ CH ].Oct ) > 8 ) Fault( "オクターブに8以上は指定できません" );
			break;
		case '>':
			/* オクターブダウン */
			if ( ( --Info[ CH ].Oct ) < 0 ) Fault( "オクターブに0以下は指定できません" );
			break;
		case '|':
			/* 標準の戻り場所 */
			break;
		case ':':
			/* 戻る */
			for ( ; MMLString > lpBuf ; MMLString-- ){
				if ( *MMLString == '|' )break;
			}
			if ( *MMLString != '|' )
				Fault( "戻る記号：に対応する｜がありません" );
			break;
		case '(':
			/* 回に応じて分岐 */
			{
				/* 何括弧か取得 */
				BYTE Route , ToCnt;
				GetNum( &MMLString , &Route );
				ToCnt = *( MMLString ) - ')';	// ここが０なら始めてきた。
				*( MMLString ++ ) += 1;			// 足跡を残す

				for ( int i = 0 ; i < ToCnt ; i++ ){
					MMLString = strchr( MMLString , ')' ) + 1;
				}
			}
			break;
		case '/':
			GetNum( &MMLString , &CH );
			if ( !Range( CH , 1 , CHs ) )Fault( "チャンネルに1〜6以外は指定できません" );
			CH--;
			break;
		case 'L':
			/* 音調絶対指定 */ 
			GetNum( &MMLString , &Info[ CH ].L );
			if ( !Range( Info[ CH ].L , 1  , 64 ) )Fault( "音長に1〜64以外は指定できません" );
			Info[ CH ].L = 192 / Info[ CH ].L;
			break;
		case 'V':
			/* 音量絶対指定 */
			GetNum( &MMLString , &Info[ CH ].Vol );
			if ( !Range( Info[ CH ].Vol , 1, 15 ) )Fault( "音量に1〜15以外は指定できません" );
			break;
		case 'O':
			/* オクターブ絶対指定 */
			GetNum( &MMLString , &Info[ CH ].Oct );
			if ( !Range( Info[CH].Oct , 1 , 8 ) )
				Fault( "オクターブに1〜8以外は指定できません" );
			break;
		case 'T':
			/* テンポ絶対指定 */
			GetNum( &MMLString , &Info[ CH ].Tempo );
			if ( !Range( Info[ CH ].Tempo , 32, 255 ) )
				Fault( "テンポに32〜255以外は指定できません" );
			break;
		case '@':
			/* 音色指定 */			
			GetNum( &MMLString ,  &Info[ CH ].NWrite()->Note );
			if ( !Range( Info[ CH ].NWrite()->Note , 1 , 127 ) ) 
				Fault( "音色に1〜127以外は指定できません" );
			Info[ CH ].NWrite()->Wait = 0;
			Info[ CH ].NextWriteTrack ( );
			break;
		case '_':
			{
				char Find[ 2 ]= { *(MMLString++) , '¥0' };
				Info[ CH ].Capo = -6 + (int)strcspn( " G A BC D EF" , Find );
				if ( Info[ CH ].Capo == -6 ) Fault( "移調コマンドの後には基準音指定が必要です" );
				/* (オプショナル)半音あげる・下げる(+,-) */
				if ( *MMLString == '+' || *MMLString == '#' ){
					Info[ CH ].Capo++;	MMLString++;
				}else if ( *MMLString == '-' ){
					Info[ CH ].Capo--;	MMLString++;
				}
			}
			break;
		default: 
			/* 音を鳴らす */
			if ( strchr( "CDEFGABR" , *(MMLString-1) ) != NULL ){
				BYTE		L1 ,Note1 ; char Find[ 2 ]= {*(MMLString-1),'¥0'};

				/* 文字の位置から音の高さを得る */
				Note1 = (int)strcspn( " C D EF G A B" , Find );
				/* (オプショナル)半音あげる・下げる(+,-) */
				if ( *MMLString == '+' || *MMLString == '#' ){
					Note1++;	MMLString++;
				}else if ( *MMLString == '-' ){
					Note1--;	MMLString++;
				}
				// （オプショナル)コードの後の直接長さ指定
				GetNum( &MMLString , &L1 );
				/* 音の長さの指定 */
				if ( L1 == 0 ) 
					L1 = Info[ CH ].L ;	/* 設定されなければ既定の設定*/
				else 
					L1 = 192 / L1;
				/* (オプショナル)付点の設定 */
				if ( *MMLString == '.' ){
					L1 = (BYTE)( L1 * 1.5 );
					MMLString++;
				}
				/* (オプショナル)複付点の設定 */
				if ( *MMLString == '.' ){
					L1 = (BYTE)( L1 * 1.25 );
					MMLString++;
				}
				
				// ノートの情報設定
				Info[ CH ].NWrite()->Note	=	( Note1 - 1 ) + Info[ CH ].Oct * 12 + Info[ CH ].Capo;
				Info[ CH ].NWrite()->Wait	=	( long )( (float)1250 / Interval / Info[ CH ].Tempo * L1 );
				if ( Info[ CH ].NWrite( )->Wait == 0 ) Info[ CH ].NWrite()->Wait = 1;
				Info[ CH ].NWrite()->Vol	=	Info[ CH ].Vol;
				Info[ CH ].NextWriteTrack ( );
			}else
				Fault( "Unknown Command msg" );
			break;
		}
	}
	free( lpBuf );
	/* -- MIDI起動 -- */
	MMLPlay_Init( );
    _endthreadex( 0 );
	return 0;
}
/* ---------- タイマー ------------ */
void CALLBACK mmTimer(UINT,UINT,DWORD,DWORD,DWORD){
	DWORD Dat; 
	
	// MIDIが使えなければリターン
	if ( !hMidi ) return ;

	// タイムウェイティング
	for ( int CH = 0 ; CH < CHs ; CH ++ ){
		if ( !Info[ CH ].Buffers )continue;
		if (  Info[ CH ].Wt > 0 ) { --Info[ CH ].Wt; continue; }
		
		// Note Off
		if ( ( Info[ CH ].PrevPlayTrack( )->Wait ) != 0 ){
			Dat = MAKEWORD( 0x80 + CH ,  Info[ CH ].PrevPlayTrack( )-> Note ) ;
			midiOutShortMsg(hMidi,Dat);
			 Info[ CH ].PrevPlayTrack( )->Wait = 0;
		}
		// 音色変更かチェック（Wait=0 then changes) 
		while ( Info[ CH ].NPlay( )->Wait == 0 ){
			if ( Info[ CH ].NP == Info[ CH ].NW ) break;
			/* 音色変更コマンド発行 */
			Dat=MAKEWORD( 0xC0  + CH , Info[ CH ].NPlay( )->Note );
			midiOutShortMsg( hMidi , Dat );
			Info[ CH ].NextPlayTrack( );
		}
		// Note On キー送信
		if ( Info[ CH ].NP != Info[ CH ].NW ){
			Dat=MAKELONG( MAKEWORD( 0x90 + CH, Info[ CH ].NPlay( )->Note ) , 
				Info[ CH ].NPlay( )->Vol * 8 );
				midiOutShortMsg(hMidi,Dat);
			Info[ CH ].Wt = Info[ CH ].NPlay( )->Wait - 1;
			Info[ CH ].NextPlayTrack( );
		}else{
			Info[ CH ].BufFree( );
			if ( !MMLPlay_State( ) )	MMLPlay_Quit( );
		}
	}
}
INT_PTR CALLBACK DialogProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam ){
	switch ( uMsg ){
	case WM_INITDIALOG:
		{
			MIDIOUTCAPS Devs;
			UINT Nums =  midiOutGetNumDevs( );	// カウンタ

			for ( UINT i = 0 ; i < Nums ; i++ ){
				midiOutGetDevCaps( i , &Devs , sizeof( Devs ) );
				SendMessage( GetDlgItem( hwndDlg , IDC_DEVLIST ) , 
					LB_INSERTSTRING , -1 , (LPARAM)Devs.szPname );
				}
			EnableWindow( GetDlgItem( hwndDlg , IDOK ) , FALSE );
			EnableWindow( GetDlgItem( hwndDlg , IDTEST ) , FALSE );
		}
		return TRUE;
	case WM_COMMAND:
		if ( HIWORD( wParam ) == BN_CLICKED ){
			HKEY RegEntry;LONG Index;

			switch ( LOWORD( wParam ) ){
			case IDOK:
				RegCreateKeyEx( HKEY_CURRENT_USER , "Software¥¥CReN" , 0 , NULL , REG_OPTION_NON_VOLATILE ,
					KEY_WRITE , NULL , &RegEntry , NULL );
				Index = (LONG)SendMessage( GetDlgItem( hwndDlg , IDC_DEVLIST ) , LB_GETCURSEL , 0 ,  0 );
				RegSetValueEx( RegEntry , "" , 0 , REG_DWORD , (const BYTE*)&Index ,   4 );
				RegCloseKey( RegEntry );
				EndDialog( hwndDlg , 0 );
				return TRUE;
			case IDCANCEL:
				EndDialog( hwndDlg , 0 );
				return TRUE;
			}
		}else if ( LOWORD( wParam ) == IDC_DEVLIST ) {
			char szCaption[512] , szType[ 256 ];LONG Index;
			Index = (LONG)SendMessage( GetDlgItem( hwndDlg , IDC_DEVLIST ) , LB_GETCURSEL , 0 ,  0 );

			EnableWindow( GetDlgItem( hwndDlg , IDTEST ) , TRUE );
			EnableWindow( GetDlgItem( hwndDlg , IDOK ) , TRUE );
			MIDIOUTCAPS Devs;
			midiOutGetDevCaps( Index , &Devs , sizeof( Devs ) );

			switch( Devs.wTechnology ){
			case MOD_MAPPER :
				lstrcpy( szType , "標準出力（マイクロソフト ミディマッパー) " );
				break;
			case MOD_MIDIPORT:
				lstrcpy( szType , "ハードウェアＭＩＤＩポート" );
				break;
			case MOD_SYNTH :
				lstrcpy( szType , "シンセサイザー" );
				break;
			case MOD_SWSYNTH :
				lstrcpy( szType , "ソフトウェア　シンセサイザー" );
				break;
			case MOD_SQSYNTH :
				lstrcpy( szType , "Ｗａｖｅ　シンセサイザー" );
				break;
			case MOD_WAVETABLE:
				lstrcpy( szType , "ＷＡＶＥテーブル　シンセサイザー" );
				break;
			case MOD_FMSYNTH :
				lstrcpy( szType , "ＦＭ音源" );
				break;
			}
			wsprintf( szCaption , "Device : %s ¥nType : %s" , Devs.szPname , szType );
			
			SetWindowText( GetDlgItem( hwndDlg , IDC_DEV2 ) , szCaption );
		}
	}
	return FALSE;
}
/* -- First Boot -- Entry */
BOOL WINAPI DllMain( HINSTANCE hDLL, DWORD dwReason, LPVOID lpReserved){
	switch ( dwReason ){
	case DLL_PROCESS_ATTACH:
		/* -- 変数初期化 -- */
		{
			for ( int CH = 0 ; CH  < CHs ; CH ++ ){
				Info[ CH ].L = 192 / 4;
				Info[ CH ].Oct = 4;
				Info[ CH ].Tempo = 120;
				Info[ CH ].Vol = 15;
				Info[ CH ].Capo = 0 ;
				Info[ CH ].Wt = 0;
			}
		}
		hDLLInst = hDLL;
		hMidi = NULL ;iTimerID = NULL;
		break;
	case DLL_PROCESS_DETACH:
		MMLPlay_Quit( );
		break;
	}
	return TRUE;
}

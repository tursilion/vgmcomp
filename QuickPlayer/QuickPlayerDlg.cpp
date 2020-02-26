// QuickPlayerDlg.cpp : implementation file
// The player loads in low RAM at >2100,
// the song data loads in high RAM at >A000 to allow 24k
// regs are in scratchpad, of course.

#include "stdafx.h"
#include "QuickPlayer.h"
#include "QuickPlayerDlg.h"
#include "QUIKPLAY.INC"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

unsigned char *textbuffer=NULL;

// Assuming PROGRAM image, no 6 byte header
void DoTIFILES(FILE *fp, int nSize) {
	/* create TIFILES header */
	unsigned char h[128];						// header

	memset(h, 0, 128);							// initialize
	h[0] = 7;
	h[1] = 'T';
	h[2] = 'I';
	h[3] = 'F';
	h[4] = 'I';
	h[5] = 'L';
	h[6] = 'E';
	h[7] = 'S';
	h[8] = 0;									// length in sectors HB
	h[9] = nSize/256;							// LB (24*256=6144)
	if (nSize%256) h[9]++;
	h[10] = 1;									// File type (1=PROGRAM)
	h[11] = 0;									// records/sector
	if (nSize%256) {
		h[12]=nSize%256;						// # of bytes in last sector (0=256)
	} else {
		h[12] = 0;								// # of bytes in last sector (0=256)
	}
	h[13] = 1;									// record length 
	h[14] = h[9];								// # of records(FIX)/sectors(VAR) LB!
	h[15] = 0;									// HB 
	fwrite(h, 1, 128, fp);
}

// CQuickPlayerDlg dialog
CQuickPlayerDlg::CQuickPlayerDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CQuickPlayerDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CQuickPlayerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CQuickPlayerDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDOK, &CQuickPlayerDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CQuickPlayerDlg::OnBnClickedCancel)
	ON_BN_CLICKED(IDC_BUTTON1, &CQuickPlayerDlg::OnBnClickedButton1)
	ON_BN_CLICKED(IDC_BUTTON2, &CQuickPlayerDlg::OnBnClickedButton2)
	ON_BN_CLICKED(IDC_BUTTON3, &CQuickPlayerDlg::OnBnClickedButton3)
END_MESSAGE_MAP()


// CQuickPlayerDlg message handlers

BOOL CQuickPlayerDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// TODO: Add extra initialization here

	return TRUE;  // return TRUE  unless you set the focus to a control
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CQuickPlayerDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CQuickPlayerDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

// disable enter and esc
void CQuickPlayerDlg::OnBnClickedOk()
{
}
void CQuickPlayerDlg::OnBnClickedCancel()
{
}

void CQuickPlayerDlg::OnBnClickedButton1()
{
	// browse
	CFileDialog dlg(TRUE, "Packed VGM", NULL, 4|2, "SPF Files|*.SPF||", this);

	if (IDOK == dlg.DoModal()) {
		CWnd *pTxt=GetDlgItem(IDC_EDIT1);
		if (NULL != pTxt) pTxt->SetWindowText(dlg.GetPathName());
	}

}

void CQuickPlayerDlg::OnBnClickedButton2()
{
	// build
	CString text[24];
	CString rawtext;
	char song[24*1024];
	size_t songsize;
	CString file;
	int idx;

	CWnd *pTxt=GetDlgItem(IDC_EDIT1);
	if (NULL == pTxt) {
		AfxMessageBox("Internal error. Can not proceed.");
		return;
	}
	pTxt->GetWindowText(file);

    CWnd *pLoop = GetDlgItem(IDC_LOOP);
	if (NULL == pTxt) {
		AfxMessageBox("Internal error. Can not proceed.");
		return;
	}
    bool loop = ((CButton*)pLoop)->GetCheck() == BST_CHECKED;

	FILE *fp;
	fp=fopen(file, "rb");
	if (NULL == fp) {
		AfxMessageBox("Can't load file.");
		return;
	}
	songsize=fread(song, 1, (24*1024)-(6*3), fp);
	if (!feof(fp)) {
		fclose(fp);
        if (songsize == 0) {
            AfxMessageBox("Error reading song.");
        } else {
    		AfxMessageBox("Song too large (24558 bytes max for this tool).");
        }
		return;
	}
	fclose(fp);
	if (songsize&1) songsize++;		// pad to word size (can't overflow cause odd->even)

	pTxt=GetDlgItem(IDC_EDIT2);
	if (NULL == pTxt) {
		AfxMessageBox("Internal error 2. Can not proceed.");
		return;
	}
	pTxt->GetWindowText(rawtext);
	
	for (idx=0; idx<24; idx++) {
		int pos=rawtext.Find("\r\n");
		if (-1 == pos) break;
		text[idx]=rawtext.Left(pos+1);
		text[idx].TrimRight("\r\n ");
		rawtext=rawtext.Mid(pos+2);
	}
	if (idx<24) {
		int pos=rawtext.Find("\r\n");
		if (-1 != pos) {
			AfxMessageBox("Too much text!");
			return;
		}
		text[idx]=rawtext;
	}
	for (idx=0; idx<24; idx++) {
		if (text[idx].GetLength() > 30) {
			AfxMessageBox("Text string too long - 30 chars per line max");
			return;
		}
	}

	// find the location to dump the text
	unsigned char *p;
	if (NULL == textbuffer) {
		p=quickplay;
		for (idx=0; idx<1024; idx++) {
			if (0 == memcmp(p, "~~~~DATAHERE~~~~", 12)) break;
			p++;
		}
		if (idx>=1024) {
			AfxMessageBox("Internal error - can't find text buffer. Failing.");
			return;
		}
		textbuffer=p;
	} else {
		p=textbuffer;
	}
	
	for (idx=0; idx<24; idx++) {
		int l=text[idx].GetLength();
		if (l<1) {
			text[idx]=" ";
			l=1;
		}
		if (l&0x01) {
			text[idx]+=' ';
			l++;
		}
        if (l > 30) l=30;
		*(p++)=l;
		memcpy(p, text[idx].GetBuffer(), l);
		p+=l;
        *(p++)='\0';
	}
	*(p++)=0;

	CFileDialog dlg(FALSE);
	if (IDOK == dlg.DoModal()) {
		CString outname=dlg.GetPathName();
		// write the player first. It's largely set up already
		// we just make sure it's set to load another file
		quickplay[128]=0xff;
		quickplay[129]=0xff;
        // NOTE: if you change the output, put a proper loop flag in it
        // rather than hacking the binary as here...
        if (loop) {
            if ((quickplay[0x13e]!=0x28)||(quickplay[0x13f]!=0x0c)||(quickplay[0x182]!=0x21)||(quickplay[0x183]!=0xc2)) {
                AfxMessageBox("Binary mismatch, can't patch loop. Contact Tursi.");
                return;
            }
            quickplay[0x183] = 0xb0;    // back to stinit()
        }

		// now dump it
		FILE *fp=fopen(outname, "wb");
		if (NULL == fp) {
			AfxMessageBox("Can't write output.");
			return;
		}
		fwrite(quickplay, 1, SIZE_OF_QUICKPLAY, fp);
		fclose(fp);

        // repair it if needed for the next pass
        if (loop) {
            quickplay[0x183] = 0xc2;
        }

		// increment last char of name and then write the song into that
        // the song can be up to 3 files long
        for (unsigned int offset = 0; offset < songsize; offset+=8192-6) {
		    int pos=outname.Find('.');
		    if (pos==-1) {
			    pos=outname.GetLength()-1;
		    } else {
			    pos--;
			    if (pos < 0) {
				    AfxMessageBox("Bad output filename");
				    return;
			    }
		    }
		    outname.SetAt(pos, outname[pos]+1);
		    fp=fopen(outname, "wb");
		    if (NULL == fp) {
			    AfxMessageBox("Can't write output.");
			    return;
		    }
            int outputsize = (songsize-offset > 8192-6 ? 8192 : (songsize-offset)+6);
		    DoTIFILES(fp, outputsize);
		    // now write the 6 byte program files header
            if (songsize <= outputsize+offset) {
		        fputc(0x00,fp);
		        fputc(0x00,fp);		// >0000 - last file 
            } else {
                fputc(0xff,fp);
                fputc(0xff,fp);     // >FFFF - more files
            }
		    fputc(outputsize/256, fp);
		    fputc(outputsize%256, fp);	// size of file including header
		    // A000 and up - load address
            int outputadr = 0xa000 + offset;
		    fputc((outputadr/256)&0xff,fp);
		    fputc((outputadr%256),fp);
		    fwrite(&song[offset], 1, outputsize-6, fp);
		    fclose(fp);
        }
	}
}

void CQuickPlayerDlg::OnBnClickedButton3()
{
	// cancel
	EndDialog(IDCANCEL);
}

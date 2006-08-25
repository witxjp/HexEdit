#if !defined(AFX_REGISTER_H__AAFC4281_06F5_11D4_A240_0020AFDC3196__INCLUDED_)
#define AFX_REGISTER_H__AAFC4281_06F5_11D4_A240_0020AFDC3196__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Register.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CAbout dialog

class CAbout : public CDialog
{
// Construction
public:
	CAbout(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CAbout)
	enum { IDD = IDD_ABOUT };
	CButton	activate_ctrl_;
	CBCGURLLinkButton	url_ctl_;
	CString	line1_;
	CString	line2_;
	CString	line3_;
	CString	line4_;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAbout)
	public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CAbout)
	virtual BOOL OnInitDialog();
	afx_msg void OnActivate();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
	void fix_controls();
};

/////////////////////////////////////////////////////////////////////////////
// CCodeEdit window - subclassed edit control for use in CRegister below

class CCodeEdit : public CEdit
{
// Construction
public:
	CCodeEdit();

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CCodeEdit)
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CCodeEdit();

	// Generated message map functions
protected:
	//{{AFX_MSG(CCodeEdit)
	afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()

    void add_spaces();
};

/////////////////////////////////////////////////////////////////////////////
// CRegister dialog

class CRegister : public CDialog
{
// Construction
public:
	CRegister(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CRegister)
	enum { IDD = IDD_REGISTER };
	CString	user_name_;
	int		type_;
	CString	install_code_;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CRegister)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CRegister)
	virtual void OnOK();
	afx_msg void OnEmail();
	afx_msg void OnMachine();
	afx_msg void OnUser();
	afx_msg void OnChangeInstallCode();
	afx_msg void OnChangeUserName();
	virtual BOOL OnInitDialog();
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
	afx_msg void OnRegHelp();
	afx_msg void OnClose();
	virtual void OnCancel();
	//}}AFX_MSG
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	DECLARE_MESSAGE_MAP()

    CCodeEdit edit_;
};

#if 0   // No longer used - most functionality moved to CStartup
/////////////////////////////////////////////////////////////////////////////
// CReg dialog

class CReg : public CDialog
{
// Construction
public:
	CReg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CReg)
	enum { IDD = IDD_REG };
	CString	user_name_;
	int		type_;
	CString	costs_;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CReg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CReg)
	virtual BOOL OnInitDialog();
	afx_msg void OnUser();
	afx_msg void OnMachine();
	afx_msg void OnChangeUserName();
	virtual void OnOK();
	afx_msg void OnRegWeb();
	afx_msg void OnRegEmail();
	afx_msg void OnRegHelp();
	afx_msg void OnClose();
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
	afx_msg void OnRegForm();
	//}}AFX_MSG
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	DECLARE_MESSAGE_MAP()
};
#endif


/////////////////////////////////////////////////////////////////////////////
// CStartup dialog

class CStartup : public CDialog
{
// Construction
public:
	CStartup(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CStartup)
	enum { IDD = IDD_STARTUP };
	CString	text_;
	//}}AFX_DATA
	CBCGURLLinkButton	url_ctl_;

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CStartup)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CStartup)
	afx_msg void OnMore();
	virtual BOOL OnInitDialog();
	afx_msg void OnRegEmail();
	afx_msg void OnRegForm();
	afx_msg void OnRegHelp();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};
//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_REGISTER_H__AAFC4281_06F5_11D4_A240_0020AFDC3196__INCLUDED_)
using EnvDTE;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using System.Linq;
    
namespace LLVM.ClangFormat
{
    // Exposes event sources for IVsRunningDocTableEvents3 events.
    internal sealed class RunningDocTableEventsDispatcher : IVsRunningDocTableEvents3
    {
        private RunningDocumentTable _runningDocumentTable;
        private DTE _dte;

        public delegate void OnBeforeSaveHander(object sender, Document document);
        public event OnBeforeSaveHander BeforeSave;

        public RunningDocTableEventsDispatcher(Package package)
        {
            _runningDocumentTable = new RunningDocumentTable(package);
            _runningDocumentTable.Advise(this);
            _dte = (DTE)Package.GetGlobalService(typeof(DTE));
        }

        public int OnAfterAttributeChange(uint docCookie, uint grfAttribs)
        {
            return VSConstants.S_OK;
        }

        public int OnAfterAttributeChangeEx(uint docCookie, uint grfAttribs, IVsHierarchy pHierOld, uint itemidOld, string pszMkDocumentOld, IVsHierarchy pHierNew, uint itemidNew, string pszMkDocumentNew)
        {
            return VSConstants.S_OK;
        }

        public int OnAfterDocumentWindowHide(uint docCookie, IVsWindowFrame pFrame)
        {
            return VSConstants.S_OK;
        }

        public int OnAfterFirstDocumentLock(uint docCookie, uint dwRDTLockType, uint dwReadLocksRemaining, uint dwEditLocksRemaining)
        {
            return VSConstants.S_OK;
        }

        public int OnAfterSave(uint docCookie)
        {
            return VSConstants.S_OK;
        }

        public int OnBeforeDocumentWindowShow(uint docCookie, int fFirstShow, IVsWindowFrame pFrame)
        {
            return VSConstants.S_OK;
        }

        public int OnBeforeLastDocumentUnlock(uint docCookie, uint dwRDTLockType, uint dwReadLocksRemaining, uint dwEditLocksRemaining)
        {
            return VSConstants.S_OK;
        }

        public int OnBeforeSave(uint docCookie)
        {
            if (BeforeSave != null)
            {
                var document = FindDocumentByCookie(docCookie);
                if (document != null) // Not sure why this happens sometimes
                {
                    BeforeSave(this, FindDocumentByCookie(docCookie));
                }
            }
            return VSConstants.S_OK;
        }

        private Document FindDocumentByCookie(uint docCookie)
        {
            var documentInfo = _runningDocumentTable.GetDocumentInfo(docCookie);
            return _dte.Documents.Cast<Document>().FirstOrDefault(doc => doc.FullName == documentInfo.Moniker);
        }
    }
}

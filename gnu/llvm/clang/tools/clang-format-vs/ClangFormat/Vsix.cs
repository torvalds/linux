using EnvDTE;
using Microsoft.VisualStudio.Editor;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.TextManager.Interop;
using System;
using System.IO;

namespace LLVM.ClangFormat
{
    internal sealed class Vsix
    {
        /// <summary>
        /// Returns the currently active view if it is a IWpfTextView.
        /// </summary>
        public static IWpfTextView GetCurrentView()
        {
            // The SVsTextManager is a service through which we can get the active view.
            var textManager = (IVsTextManager)Package.GetGlobalService(typeof(SVsTextManager));
            IVsTextView textView;
            textManager.GetActiveView(1, null, out textView);

            // Now we have the active view as IVsTextView, but the text interfaces we need
            // are in the IWpfTextView.
            return VsToWpfTextView(textView);
        }

        public static bool IsDocumentDirty(Document document)
        {
            var textView = GetDocumentView(document);
            var textDocument = GetTextDocument(textView);
            return textDocument?.IsDirty == true;
        }

        public static IWpfTextView GetDocumentView(Document document)
        {
            var textView = GetVsTextViewFrompPath(document.FullName);
            return VsToWpfTextView(textView);
        }

        public static IWpfTextView VsToWpfTextView(IVsTextView textView)
        {
            var userData = (IVsUserData)textView;
            if (userData == null)
                return null;
            Guid guidWpfViewHost = DefGuidList.guidIWpfTextViewHost;
            object host;
            userData.GetData(ref guidWpfViewHost, out host);
            return ((IWpfTextViewHost)host).TextView;
        }

        public static IVsTextView GetVsTextViewFrompPath(string filePath)
        {
            // From http://stackoverflow.com/a/2427368/4039972
            var dte2 = (EnvDTE80.DTE2)Package.GetGlobalService(typeof(SDTE));
            var sp = (Microsoft.VisualStudio.OLE.Interop.IServiceProvider)dte2;
            var serviceProvider = new Microsoft.VisualStudio.Shell.ServiceProvider(sp);

            IVsUIHierarchy uiHierarchy;
            uint itemID;
            IVsWindowFrame windowFrame;
            if (VsShellUtilities.IsDocumentOpen(serviceProvider, filePath, Guid.Empty,
                out uiHierarchy, out itemID, out windowFrame))
            {
                // Get the IVsTextView from the windowFrame.
                return VsShellUtilities.GetTextView(windowFrame);
            }
            return null;
        }

        public static ITextDocument GetTextDocument(IWpfTextView view)
        {
            ITextDocument document;
            if (view != null && view.TextBuffer.Properties.TryGetProperty(typeof(ITextDocument), out document))
                return document;
            return null;
        }

        public static string GetDocumentParent(IWpfTextView view)
        {
            ITextDocument document = GetTextDocument(view);
            if (document != null)
            {
                return Directory.GetParent(document.FilePath).ToString();
            }
            return null;
        }

        public static string GetDocumentPath(IWpfTextView view)
        {
            return GetTextDocument(view)?.FilePath;
        }
    }
}

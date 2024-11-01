<!-- manpage-normal.xsl:
     special settings for manpages rendered from asciidoc+docbook
     handles anything we want to keep away from docbook-xsl 1.72.0 -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
		version="1.0">

<xsl:import href="manpage-base.xsl"/>

<!-- these are the normal values for the roff control characters -->
<xsl:param name="git.docbook.backslash">\</xsl:param>
<xsl:param name="git.docbook.dot"	>.</xsl:param>

</xsl:stylesheet>

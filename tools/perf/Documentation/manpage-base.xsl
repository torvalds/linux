<!-- manpage-base.xsl:
     special formatting for manpages rendered from asciidoc+docbook -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
		version="1.0">

<!-- these params silence some output from xmlto -->
<xsl:param name="man.output.quietly" select="1"/>
<xsl:param name="refentry.meta.get.quietly" select="1"/>

<!-- convert asciidoc callouts to man page format;
     git.docbook.backslash and git.docbook.dot params
     must be supplied by another XSL file or other means -->
<xsl:template match="co">
	<xsl:value-of select="concat(
			      $git.docbook.backslash,'fB(',
			      substring-after(@id,'-'),')',
			      $git.docbook.backslash,'fR')"/>
</xsl:template>
<xsl:template match="calloutlist">
	<xsl:value-of select="$git.docbook.dot"/>
	<xsl:text>sp&#10;</xsl:text>
	<xsl:apply-templates/>
	<xsl:text>&#10;</xsl:text>
</xsl:template>
<xsl:template match="callout">
	<xsl:value-of select="concat(
			      $git.docbook.backslash,'fB',
			      substring-after(@arearefs,'-'),
			      '. ',$git.docbook.backslash,'fR')"/>
	<xsl:apply-templates/>
	<xsl:value-of select="$git.docbook.dot"/>
	<xsl:text>br&#10;</xsl:text>
</xsl:template>

</xsl:stylesheet>

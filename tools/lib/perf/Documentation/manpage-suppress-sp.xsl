<!-- manpage-suppress-sp.xsl:
     special settings for manpages rendered from asciidoc+docbook
     handles erroneous, inline .sp in manpage output of some
     versions of docbook-xsl -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
		version="1.0">

<!-- attempt to work around spurious .sp at the tail of the line
     that some versions of docbook stylesheets seem to add -->
<xsl:template match="simpara">
  <xsl:variable name="content">
    <xsl:apply-templates/>
  </xsl:variable>
  <xsl:value-of select="normalize-space($content)"/>
  <xsl:if test="not(ancestor::authorblurb) and
                not(ancestor::personblurb)">
    <xsl:text>&#10;&#10;</xsl:text>
  </xsl:if>
</xsl:template>

</xsl:stylesheet>

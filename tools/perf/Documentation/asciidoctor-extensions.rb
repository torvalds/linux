require 'asciidoctor'
require 'asciidoctor/extensions'

module Perf
  module Documentation
    class LinkPerfProcessor < Asciidoctor::Extensions::InlineMacroProcessor
      use_dsl

      named :chrome

      def process(parent, target, attrs)
        if parent.document.basebackend? 'html'
          %(<a href="#{target}.html">#{target}(#{attrs[1]})</a>\n)
        elsif parent.document.basebackend? 'manpage'
          "#{target}(#{attrs[1]})"
        elsif parent.document.basebackend? 'docbook'
          "<citerefentry>\n" \
            "<refentrytitle>#{target}</refentrytitle>" \
            "<manvolnum>#{attrs[1]}</manvolnum>\n" \
          "</citerefentry>\n"
        end
      end
    end
  end
end

Asciidoctor::Extensions.register do
  inline_macro Perf::Documentation::LinkPerfProcessor, :linkperf
end

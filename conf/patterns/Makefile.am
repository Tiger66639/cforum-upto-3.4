EXTRA_DIST=apache.pat c.pat css.pat html.pat java.pat javascript.pat pascal.pat perl.pat php.pat python.pat ruby.pat sql.pat xml.pat

install-data-local:
	if test ! -d $(DESTDIR)$(sysconfdir)/cforum/patterns; then \
		$(MKDIR) -p $(DESTDIR)$(sysconfdir)/cforum/patterns; \
	fi; \
	for file in $(EXTRA_DIST); do \
		$(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/cforum/patterns/$$file; \
	done

# eof

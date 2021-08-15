# $Id: Makefile,v 1.5 2007/05/12 15:04:26 ecd Exp $

locale = /usr/local/share/locale

XGETTEXT = xgettext
MSGMERGE = msgmerge
MSGFMT = msgfmt

PACKAGE = radarplot

LANGUAGES = de

CATALOGS = $(patsubst %,%.po,$(LANGUAGES))

POTFILES = radar.c print.c register.c
VPATH = ..

all: $(CATALOGS)

$(CATALOGS): $(PACKAGE).pot
	@for cat in $(CATALOGS); do \
		lang=`basename $$cat | sed -e 's/\.po$$//'`; \
		echo -n "msgmerge $$lang.po $(PACKAGE).pot: "; \
		if $(MSGMERGE) $$lang.po $(PACKAGE).pot -o $$lang.new.po; then \
			if cmp $$lang.po $$lang.new.po >/dev/null 2>&1; then \
				touch $$lang.po; \
				rm -f $$lang.new.po; \
			else \
				if mv -f $$lang.new.po $$lang.po; then \
					:; \
				else \
					echo "msgmerge for $$lang.po failed: cannot mv $$lang.new.po $$lang.po" 1>&2; \
					rm -f $$lang.new.po; \
					exit 1; \
				fi; \
			fi; \
		else \
			echo "msgmerge for $$lang.po failed!"; \
			rm -f $$lang.new.po; \
		fi; \
	done

$(PACKAGE).pot: $(POTFILES)
	$(XGETTEXT) --default-domain=$(PACKAGE) --directory=.. \
		--foreign-user --keyword=_ --keyword=N_ --keyword=Q_ \
		--copyright-holder="Eddie C. Dost <ecd@brainaid.de>" \
		--add-comments --msgid-bugs-address=ecd@brainaid.de \
		--from-code=iso-8859-1 $(POTFILES)
	@if test -f $(PACKAGE).po; then \
		cat $(PACKAGE).pot | \
			sed -e 's/"\(POT-Creation-Date: \)[^"]*"/"\1"/' \
				> $(PACKAGE).test.pot; \
		cat $(PACKAGE).po | \
			sed -e 's/"\(POT-Creation-Date: \)[^"]*"/"\1"/' \
				> $(PACKAGE).test.po; \
		if cmp $(PACKAGE).test.pot $(PACKAGE).test.po >/dev/null 2>&1; then \
			touch $(PACKAGE).pot; \
			rm -f $(PACKAGE).po; \
		else \
			if mv -f $(PACKAGE).po $(PACKAGE).pot; then \
				:; \
			else \
				echo "xgettext for $(PACKAGE).pot failed: cannot mv $(PACKAGE).po $(PACKAGE).pot" 1>&2; \
				rm -f $(PACKAGE).test.pot; \
				rm -f $(PACKAGE).test.po; \
				rm -f $(PACKAGE).po; \
				exit 1; \
			fi; \
		fi; \
		rm -f $(PACKAGE).test.pot; \
		rm -f $(PACKAGE).test.po; \
	fi

install: $(CATALOGS)
	@for cat in $(CATALOGS); do \
		lang=`basename $$cat | sed -e 's/\.po$$//'`; \
		MESSAGES=$(locale)/$$lang/LC_MESSAGES; \
		echo "install -m 644 $$lang.po $$MESSAGES/$(PACKAGE).mo"; \
		install -d $$MESSAGES; \
		$(MSGFMT) $$lang.po -o $$MESSAGES/$(PACKAGE).mo; \
		chmod 644 $$MESSAGES/$(PACKAGE).mo; \
	done

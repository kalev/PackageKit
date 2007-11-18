#ifndef _ZYPP_EVENTS_H_
#define _ZYPP_EVENTS_H_

#include <stdio.h>
#include <glib.h>
#include <pk-backend.h>
#include <zypp/ZYppCallbacks.h>


namespace ZyppBackend
{

struct ZyppBackendReceiver
{
	PkBackend *_backend;
	gchar *_package_id;
	guint _sub_percentage;

	virtual void initWithBackend (PkBackend *backend)
	{
		_backend = backend;
		_package_id = NULL;
		_sub_percentage = 0;
	}

	virtual void clear_package_id ()
	{
		if (_package_id != NULL) {
			g_free (_package_id);
			_package_id = NULL;
		}
	}

	/**
	 * Build a package_id from the specified resolvable.  The returned
	 * gchar * should be freed with g_free ().
	 */
	gchar *
	build_package_id_from_resolvable (zypp::Resolvable::constPtr resolvable)
	{
		gchar *package_id;
		
		package_id = pk_package_id_build (resolvable->name ().c_str (),
						  resolvable->edition ().asString ().c_str (),
						  resolvable->arch ().asString ().c_str (),
						  "opensuse");

		return package_id;
	}

	/**
	 * Build a package_id from the specified zypp::Url.  The returned
	 * gchar * should be freed with g_free ().  Returns NULL if the
	 * URL does not contain information about an RPM.
	 *
	 * Example:
	 *    basename: lynx-2.8.6-63.i586.rpm
	 *    result:   lynx;2.8.6-63;i586;opensuse
	 */
	gchar *
	build_package_id_from_url (const zypp::Url *url)
	{
		gchar *package_id;
		gchar *basename;
		gchar *tmp;
	
		gchar *arch;
		gchar *edition;
		gchar *name;
		gboolean first_dash_found;
	
		basename = g_strdup (zypp::Pathname (url->getPathName ()).basename ().c_str());
	
		tmp = g_strrstr (basename, ".rpm");
	
		if (tmp == NULL) {
			g_free (basename);
			return NULL;
		}
	
		// Parse the architecture
		tmp [0] = '\0'; // null-terminate the arch section
		for (tmp--; tmp != basename && tmp [0] != '.'; tmp--) {}
		arch = tmp + 1;
	
		// Parse the edition
		tmp [0] = '\0'; // null-terminate the edition (version)
		first_dash_found = FALSE;
		for (tmp--; tmp != basename; tmp--) {
			if (tmp [0] == '-') {
				if (first_dash_found == FALSE) {
					first_dash_found = TRUE;
					continue;
				} else {
					break;
				}
			}
		}
		edition = tmp + 1;
	
		// Parse the name
		tmp [0] = '\0'; // null-terminate the name
		name = basename;
	
		package_id = pk_package_id_build (name, edition, arch, "opensuse");
		g_free (basename);
	
		return package_id;
	}

	inline void
	update_sub_percentage (guint percentage)
	{
		// TODO: Figure out this weird bug that libzypp emits a 100
		// at the beginning of installing a package.
		if (_sub_percentage == 0 && percentage == 100)
			return; // can't jump from 0 -> 100 instantly!

		// Only emit a percentage if it's different from the last
		// percentage we emitted and it's divisible by ten.  We
		// don't want to overload dbus/GUI.  Also account for the
		// fact that libzypp may skip over a "divisible by ten"
		// value (i.e., 28, 29, 31, 32).

		// Drop off the least significant digit
		// TODO: Figure out a faster way to drop the least significant digit
		percentage = (percentage / 10) * 10;

		if (percentage <= _sub_percentage)
			return;

		_sub_percentage = percentage;
		pk_backend_change_sub_percentage (_backend, _sub_percentage);
	}

	void
	reset_sub_percentage ()
	{
		_sub_percentage = 0;
		pk_backend_change_sub_percentage (_backend, _sub_percentage);
	}
};

struct InstallResolvableReportReceiver : public zypp::callback::ReceiveReport<zypp::target::rpm::InstallResolvableReport>, ZyppBackendReceiver
{
	zypp::Resolvable::constPtr _resolvable;

	virtual void start (zypp::Resolvable::constPtr resolvable)
	{
		clear_package_id ();
		_package_id = build_package_id_from_resolvable (resolvable);
		fprintf (stderr, "\n\n----> InstallResolvableReportReceiver::start(): %s\n\n", _package_id == NULL ? "unknown" : _package_id);
		if (_package_id != NULL) {
			pk_backend_change_status (_backend, PK_STATUS_ENUM_INSTALL);
			pk_backend_package (_backend, PK_INFO_ENUM_INSTALLING, _package_id, "TODO: Put the package summary here if possible");
			reset_sub_percentage ();
		}
	}

	virtual bool progress (int value, zypp::Resolvable::constPtr resolvable)
	{
		fprintf (stderr, "\n\n----> InstallResolvableReportReceiver::progress(), %s:%d\n\n", _package_id == NULL ? "unknown" : _package_id, value);
		if (_package_id != NULL)
			update_sub_percentage (value);
		return true;
	}

	virtual Action problem (zypp::Resolvable::constPtr resolvable, Error error, const std::string &description, RpmLevel level)
	{
		fprintf (stderr, "\n\n----> InstallResolvableReportReceiver::problem()\n\n");
		return ABORT;
	}

	virtual void finish (zypp::Resolvable::constPtr resolvable, Error error, const std::string &reason, RpmLevel level)
	{
		fprintf (stderr, "\n\n----> InstallResolvableReportReceiver::finish(): %s\n\n", _package_id == NULL ? "unknown" : _package_id);
		if (_package_id != NULL) {
			pk_backend_package (_backend, PK_INFO_ENUM_INSTALLED, _package_id, "TODO: Put the package summary here if possible");
			clear_package_id ();
		}
	}
};

struct RepoProgressReportReceiver : public zypp::callback::ReceiveReport<zypp::ProgressReport>, ZyppBackendReceiver
{
	virtual void start (const zypp::ProgressData &data)
	{
		fprintf (stderr, "\n\n----> RepoProgressReportReceiver::start()\n\n");
	}

	virtual bool progress (const zypp::ProgressData &data)
	{
		fprintf (stderr, "\n\n----> RepoProgressReportReceiver::progress(), %s:%d\n\n", data.name().c_str(), (int)data.val());
		return true;
	}

	virtual void finish (const zypp::ProgressData &data)
	{
		fprintf (stderr, "\n\n----> RepoProgressReportReceiver::finish()\n\n");
	}
};

struct RepoReportReceiver : public zypp::callback::ReceiveReport<zypp::repo::RepoReport>, ZyppBackendReceiver
{
	virtual void start (const zypp::ProgressData &data)
	{
		fprintf (stderr, "\n\n----> RepoReportReceiver::start()\n");
	}

	virtual bool progress (const zypp::ProgressData &data)
	{
		fprintf (stderr, "\n\n----> RepoReportReceiver::progress(), %s:%d\n", data.name().c_str(), (int)data.val());
		return true;
	}

	virtual void finish (const zypp::ProgressData &data)
	{
		fprintf (stderr, "\n\n----> RepoReportReceiver::finish()\n");
	}
};

struct DownloadProgressReportReceiver : public zypp::callback::ReceiveReport<zypp::media::DownloadProgressReport>, ZyppBackendReceiver
{
	virtual void start (const zypp::Url &file, zypp::Pathname localfile)
	{
		clear_package_id ();
		_package_id = build_package_id_from_url (&file);

		fprintf (stderr, "\n\n----> DownloadProgressReportReceiver::start(): %s\n", _package_id == NULL ? "unknown" : _package_id);
		if (_package_id != NULL) {
			pk_backend_change_status (_backend, PK_STATUS_ENUM_DOWNLOAD);
			pk_backend_package (_backend, PK_INFO_ENUM_DOWNLOADING, _package_id, "TODO: Put the package summary here if possible");
			reset_sub_percentage ();
		}
	}

	virtual bool progress (int value, const zypp::Url &file)
	{
		fprintf (stderr, "\n\n----> DownloadProgressReportReceiver::progress(), %s:%d\n\n", _package_id == NULL ? "unknown" : _package_id, value);
		if (_package_id != NULL)
			update_sub_percentage (value);
		return true;
	}

	virtual void finish (const zypp::Url & file, Error error, const std::string &konreason)
	{
		fprintf (stderr, "\n\n----> DownloadProgressReportReceiver::finish(): %s\n", _package_id == NULL ? "unknown" : _package_id);
		clear_package_id ();
	}
};

}; // namespace ZyppBackend

class EventDirector
{
	private:
		EventDirector () {}
	
		ZyppBackend::RepoReportReceiver _repoReport;
		ZyppBackend::RepoProgressReportReceiver _repoProgressReport;
		ZyppBackend::InstallResolvableReportReceiver _installResolvableReport;
		ZyppBackend::DownloadProgressReportReceiver _downloadProgressReport;

	public:
		EventDirector (PkBackend *backend)
		{
			_repoReport.initWithBackend (backend);
			_repoReport.connect ();

			_repoProgressReport.initWithBackend (backend);
			_repoProgressReport.connect ();

			_installResolvableReport.initWithBackend (backend);
			_installResolvableReport.connect ();

			_downloadProgressReport.initWithBackend (backend);
			_downloadProgressReport.connect ();
		}

		~EventDirector ()
		{
			_repoReport.disconnect ();
			_repoProgressReport.disconnect ();
			_installResolvableReport.disconnect ();
			_downloadProgressReport.disconnect ();
		}
};


#endif // _ZYPP_EVENTS_H_


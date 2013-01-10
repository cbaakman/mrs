//---------------------------------------------------------------------
//
//	JavaScript to fill in a status page
//

Status =
{
	timeout: null,

	init: function()
	{
		Status.timeout = setTimeout("Status.updateStatus()", 1000);
	},

	updateStatus: function()
	{
		jQuery.getJSON("ajax/status",
			function(data, status)
			{
				if (status == "success") 
					Status.updateList(data);
			}
		);		
	},

	updateList: function(stat)
	{
		for (i in stat)
		{
			var db = stat[i];
			
			var row = document.getElementById("db-" + db.id);
			if (row == null) continue;

			if (db.update == null)
			{
				$(row).find("#active").hide();
				$(row).find("#idle").show();
			}
			else
			{
				$(row).find("#active").show();
				$(row).find("#idle").hide();

				$(row).find("#update-status").text(db.update.stage != null ? db.update.stage : "");
				
				if (db.update.stage == 'scheduled')
					row.className = 'scheduled';
				else if (db.update.stage == 'listing files' || db.update.stage == 'rsync')
					row.className = 'active';
				else if (db.update.progress < 0)
					row.className = 'error';
				else
				{
					row.className = 'active';

					// HTML 5 canvas
					$(row).find("#update-progress").each(function()
					{
						var ctx = this.getContext('2d');
						if (ctx != null)
						{
							this.style.display = '';
		
							var p = db.update.progress * 100;
							if (p > 100)
								p = 100;
		
							ctx.strokeStyle = "#2f506c";
							ctx.strokeRect(0, 0, 102, 10);
		
							ctx.fillStyle = "#c6d4e1";
							ctx.fillRect(1, 1, p, 8);
							
							ctx.fillStyle = "#ffffff";
							ctx.fillRect(p + 1, 1, 100 - p, 8);
						}
					});
				}
			}
		}
	}
}

// register a load handler
addLoadEvent(Status.init);

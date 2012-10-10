package M6::Script::pfam;

our @ISA = "M6::Script";

my @links = (
	{
		match	=> qr|^(#=GF DR\s+PFAMA;\s)(\S+)(?=;)|mo,
		db		=> 'pfama',
		ix		=> 'ac'
	},
	{
		match	=> qr|^(#=GF DR\s+PFAMB;\s)(\S+)(?=;)|mo,
		db		=> 'pfamb',
		ix		=> 'ac'
	},
	{
		match	=> qr|^(#=GF DR\s+PDB;\s)(\S+)|mo,
		db		=> 'pdb',
		ix		=> 'id'
	},
	{
		match	=> qr|^(#=GF DR\s+PROSITE;\s)(\S+)(?=;)|mo,
		db		=> 'prosite_doc',
		ix		=> 'id'
	},
	{
		match	=> qr|^(#=GF DR\s+INTERPRO;\s)(\S+)(?=;)|mo,
		db		=> 'interpro',
		ix		=> 'id'
	},
	{
		match	=> qr|^(#=GS .+?AC )([0-9A-Z]+)|mo,
		db		=> 'uniprot',
		ix		=> 'ac'
	},
	{
		match	=> qr|^(#=GS .+DR PDB; )(\w{4})|mo,
		db		=> 'pdb',
		ix		=> 'id'
	},
);

sub new
{
	my $invocant = shift;
	
	my $self = new M6::Script(
		firstdocline => '# STOCKHOLM 1.0',
		lastdocline => '//',
		@_
	);
	
	return bless $self, "M6::Script::pfam";
}

sub parse
{
	my ($self, $text) = @_;
	
	open(my $h, "<", \$text);
	while (my $line = <$h>)
	{
		chomp($line);

		if (substr($line, 0, 5) eq '#=GF ')
		{
			my $field = substr($line, 5, 2);
			my $value = substr($line, 10);

			if ($field eq 'ID')
			{
				$self->index_unique_string('id', $value);
			}
			elsif ($field =~ /BM|GA|TC|NC/o)  # useless fields
			{}
			elsif ($field eq 'AC' and $value =~ m/(P[BF]\d+)/)
			{
				$value = $1;
				$self->index_string('ac', $value);
			}
			elsif ($field eq 'DR')
			{
				my @link = split(m/; */, $value);
				$self->add_link($link[0], $link[1]) if length($link[0]) > 0 and length($link[1]) > 0;
				
				$self->index_text('ref', $value);
			}
			elsif (substr($field, 0, 1) eq 'R')
			{
				$self->index_text('ref', $value);
			}
			elsif (substr($field, 0, 2) eq 'SQ')
			{
				$self->index_number('nseq', $value);
			}
			else
			{
				$self->set_attribute('title', $value) if $field eq 'DE';
				$self->index_text(lc($field), $value);
			}
		}
		elsif (substr($line, 0, 5) eq '#=GS ')
		{
			#		#=GS Q9ZNY5_SECCE/28-72   
			my $link = substr($line, 26);
			$self->add_link('uniprot', $1) if ($line =~ m/^AC (.+?)(\.\d)/);
			$self->add_link('pdb', $1) if ($line =~ m/^DR PDB; (\w{4})/);
			
			$self->index_text('gs', substr($line, 5));
		}
	}
}

sub version
{
	my ($self) = @_;
	my $vers;

	my $raw_dir = $self->{raw_dir} or die "raw_dir is not defined\n";
	
	my $fh;

print "zcat $raw_dir/relnotes.txt.Z\n";
	open($fh, "zcat $raw_dir/relnotes.txt.Z|");

	while (my $line = <$fh>)
	{
		if ($line =~ /^\s+(RELEASE [0-9.]+)/)
		{
			$vers = $1;
			last;
		}
	}

	close($fh);

	chomp($vers);

	return $vers;
}

sub pp
{
	my ($this, $q, $text, $id, $url) = @_;
	
	$text = $this->link_url($text);

	# some entries are really way too large, so only when we have less than 1000 links:
	if ($text =~ m/^#=GF SQ\s+(\d+)/mo and int($1) < 1000)
	{
		foreach my $l (@links)
		{
			my $db = $l->{db};
			my $ix = $l->{ix};
			$text =~ s|$l->{match}|$1<mrs:link db='$db' index='$ix' id='$2'>$2</mrs:link>|g;
		}
	}
	
	return
		$q->div({-class=>'entry', 'xmlns:mrs' => 'http://mrs.cmbi.ru.nl/mrs-web/ml'},
		$q->pre($text));
}

1;

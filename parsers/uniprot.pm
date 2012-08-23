package M6::Script::uniprot;

use utf8;

our @ISA = "M6::Script";

use strict;
use warnings;

our $commentLine1 = "-----------------------------------------------------------------------";
our $commentLine2 = "Copyrighted by the UniProt Consortium, see http://www.uniprot.org/terms";
our $commentLine3 = "Distributed under the Creative Commons Attribution-NoDerivs License";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(@_);
	return bless $self, "M6::Script::uniprot";
}

sub parse
{
	my ($self, $text) = @_;
	
	my %months = (
		'JAN' => 1, 'FEB' => 2, 'MAR' => 3, 'APR' => 4, 'MAY' => 5, 'JUN' => 6,
		'JUL' => 7, 'AUG' => 8, 'SEP' => 9, 'OCT' => 10, 'NOV' => 11, 'DEC' => 12
	);
	
	while ($text =~ m/^(?:([A-Z]{2})   ).+\n(?:\1.+\n)*/gm)
	{
		my $key = $1;
		my $value = $&;
		
		$value =~ s/^$key   //mg;

		if ($key eq 'ID')
		{
			$value =~ m/^(\w+)/ or die "No ID in UniProt record?\n$key   $value\n";
			$self->index_unique_string('id', $1);
			$self->set_attribute('id', $1);
		}
		elsif ($key eq 'AC')
		{
			my $n = 0;
			foreach my $ac (split(m/;\s*/, $value))
			{
				$self->index_unique_string('ac', $ac);
				$self->set_attribute('ac', $ac) unless ++$n > 1;
			}
		}
		elsif ($key eq 'DE')
		{
			$self->set_attribute('title', $1) if ($value =~ m/Full=(.+?);/);
			$self->index_text('de', $value);

			while ($value =~ /(EC\s*)(\d+\.\d+\.\d+\.\d+)/g)
			{
				$self->add_link('enzyme', $2);
			}
		}
		elsif ($key eq 'DT')
		{
			while ($value =~ m/(\d{2})-(JAN|FEB|MAR|APR|MAY|JUN|JUL|AUG|SEP|OCT|NOV|DEC)-(\d{4})/g)
			{
				my $date = sprintf('%4.4d-%2.2d-%2.2d', $3, $months{$2}, $1);
#				$self->index_date('dt', $date);
				$self->index_string('dt', $date);
			}
		}
		elsif ($key eq 'GN')
		{
			while ($value =~ m/(?:\w+=)?(.+);/g)
			{
				$self->index_text(lc $key, $1);
			}
		}
		elsif ($key eq 'OC' or $key eq 'KW')
		{
			while ($value =~ m/(.+);/g)
			{
				$self->index_string('oc', $1);
			}
		}
		elsif ($key eq 'CC')
		{
			while ($value =~ m/.+/gm)
			{
				$self->index_text('cc', $&)
					unless ($& eq $commentLine1 or $& eq $commentLine2 or $& eq $commentLine3);
			}
		}
		elsif ($key eq 'RX')
		{
			while ($value =~ m/(MEDLINE|PubMed|DOI)=([^;]+);/g)
			{
				$self->index_string(lc $1, $2);
			}
		}
		elsif (substr($key, 0, 1) eq 'R')
		{
			$self->index_text('ref', $value);
		}
		elsif ($key eq 'DR')
		{
			while ($value =~ m/^(.+?); (.+?);/g)
			{
				$self->add_link($1, $2);
			}
			
			$self->index_text('dr', $value);
		}
		elsif ($key eq 'SQ')
		{
			if ($value =~ /SEQUENCE\s+(\d+) AA;\s+(\d+) MW;\s+([0-9A-F]{16}) CRC64;/o)
			{
				$self->index_number('length', $1);
				$self->index_number('mw', $2);
				$self->index_string('crc64', $3);
			}
			
#			my $sequence = substr($text, pos $text);
#			$sequence =~ s/\s//g;
#			$sequence =~ s|//$||;
#			$self->add_sequence($sequence);
			
			last;
		}
		elsif ($key ne 'XX')
		{
			$self->index_text(lc($key), $value);
		}
	}
}

1;
